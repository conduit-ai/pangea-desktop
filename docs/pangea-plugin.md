# The Pangea plugin

One plugin in the DAW that opens any of the member's Pangea plugins and changes to a new version while the session plays. It is the alternative to building and installing every project as its own Audio Unit: nothing is rescanned and nothing is restarted. Decision 65 says why.

It comes as three plugins, because a DAW fixes what a plugin is when it scans it:

| Plugin | AU | VST3 | Opens |
|---|---|---|---|
| **Pangea** | effect (`aufx`, code `Phfx`) | effect | effects |
| **Pangea Instrument** | instrument (`aumu`, code `Phin`), MIDI in and out | instrument | instruments and MIDI tools |
| **Pangea MIDI FX** | MIDI effect (`aumi`, code `Phmi`) | none | MIDI tools, in Logic's MIDI FX slot |

All three have the manufacturer code `Pnga`. They are built on the member's Mac, like every other plugin; nothing is signed or distributed. The desktop app's Setup screen does it with Install the Pangea plugin, and Update the Pangea plugin when its shell needs a newer one (`desktop/src-tauri/src/host_install.rs`: `shell/build-host.sh`, then the bundles copied into the plug-in folders, the registrar restarted and `auval` on all three; `docs/setup-a-mac.md`). A developer can still run `shell/build-host.sh --install` in Terminal.

## How it works

1. The plugin lists the member's plugins from `library.json`, which the desktop app keeps up to date.
2. A click asks the desktop app for that plugin's latest version. The app fetches it, checks it exactly as it checks an install, and compiles it into a **module**: the Faust code and the logic module, if any, in one small library (`shell/build-module.sh`, about a second).
3. The plugin loads the module on the message thread, prepares it, and swaps it in on the audio thread with a short crossfade. The old version is freed off the audio thread.
4. **Refresh** asks for the latest version again. If there is a newer one, it replaces the current one the same way, keeping the person's settings.
5. **Follow new versions** asks for a newer version by itself when `library.json` lists one. A song opened again keeps the version it was saved with, Follow or not (decision 83): with Follow on, the plugin waits for the first `library.json` the app writes after the song opened, holds back the versions it already lists, and follows only newer ones. Refresh, choosing the plugin, or ticking Follow again takes the newest at once.

The desktop app has to be running for the list to change and for a version to be built. Modules already built stay on disk, so a session opens without it.

## Parameters and automation

A DAW reads a plugin's parameters once, so the plugin offers a fixed bank of 128 (`p001` to `p128`, the most controls a layout may place). Each control of the loaded plugin takes a slot, and the slot takes the control's name, range and units; the plugin tells the host that the names changed (AU `kAudioUnitProperty_ParameterList`, VST3 `kParamTitlesChanged`).

- A control keeps its slot from version to version, by its id (`envamount` for "Env Amount"; `logic:` and the key for a logic control), so automation follows it. A control of an instrument's effect section is a control like any other.
- A new control takes a free slot. A removed control's slot stays reserved for that project, so old automation never drives a different control.
- Each project keeps its own slots, saved with the session.
- On a new version, a control the person has moved keeps its value; one still at its old default takes the new default, so an edit that changes a default is heard.

After the 128 slots comes one parameter more, the 129th: Pangea's bypass ("The bypass" below). It is not a slot. Slots are given out among the 128 only, so no project can take it, rename it or reset it, and its automation holds through every version and every plugin opened.

## The bypass

Every Pangea plugin has one bypass and Pangea supplies it (decisions 74 and 75). In the Pangea plugin it is the plugin's own, whatever is open in it: the parameter `pangea_bypass`, named Bypass, the same id and name a plugin built for one project has (`shell/platform/PangeaBypass.h`; `shell/README.md`, "The bypass").

- **What bypassed means** follows the version that is open. An effect passes its input, to the bit. An instrument is silent: it takes no new notes, and once the fade is over its voices are ended. A MIDI tool ends its own notes, starts none, and passes on the MIDI it is sent. A logic module in an effect or an instrument is run as if the transport had stopped.
- **Every switch is a fade of 30 ms**, after the crossfade between versions, so a new version that comes in while the plugin is bypassed is not heard. The open version runs on unheard, so an effect comes back with its tails as they are, and takes the processor time it takes when it is in.
- **A version opened while bypassed** is bypassed from its first sample. The notes that were held when the bypass began are let go once its fade is over, so an instrument opened after that is played none of them.
- **The host.** The plugin answers `getBypassParameter` with it, so a host whose own bypass tells a plugin moves it, in the VST3 and in the Audio Units. The checks show it in the VST3; for the Audio Units it is read from JUCE's code, since no check loads one. In an Audio Unit's list it comes after the slots (version hint 3, where a slot has 1), so the slots keep their places.
- **Saved with the session** by name (`bypass` on the session's root, beside `follow` and `web`), not among the slots' values. A session saved before the plugin had its bypass opens with the plugin in.
- **The plugin's version is 0.3.0** (`HOST_VERSION` in `shell/build-host.sh`; 0.2.0 when the bypass came, 0.1.0 before), so that a DAW which keeps what it read of an Audio Unit by its version reads the parameters and the buses again. The plugin has to be built and installed again for any of this (`shell/build-host.sh --install`).

## The sidechain input

Since 0.3.0 Pangea, the effect, has a second input bus, **Sidechain**, stereo and off by default (`docs/sidechain.md`). Pangea Instrument and Pangea MIDI FX have the buses they had.

- **A version whose code declares a sidechain** (`declare sidechain "stereo";` or `"mono"`) is fed from it: its inputs after the sound's two take the bus's channels, a mono bus on both sides of a stereo sidechain, a stereo bus as the mean of its sides on a mono one, and silence while the bus is off. Kick Duck ducks on a kick routed there, in the processor and in the built VST3.
- **Any other version** never reads it: Tape Drift gives the same samples with a kick on the sidechain as with the bus off.
- **A crossfade between versions** feeds both from one copy of the bus taken at the start of the block, so a version fading in hears the sidechain the one fading out did.
- **An existing session** opens with the same parameters, the same state and the same main buses: the bus is new and off, and a host that routes nothing to it never switches it on. An Audio Unit has no bus that is off, so in Logic the Side Chain menu appears in the plugin's header and reads None, and the plugin hears silence there.
- **A Pangea plugin of 0.2.0** opens a module whose code declares a sidechain all the same, and plays it with silence on the sidechain: no ducking, the sound passing as it is (read from its code, which clears every input past the host's two; not run). The module's number stays 2.

## The list

With nothing open, and after **Plugins** in the top bar, the window lists the member's plugins: what this plugin can open first, the rest fainter with the reason at the right ("Opens in Pangea Instrument", "Shared by Sam", "No version yet"). A row says the plugin's name, then its category, its version and its summary: "Delay, v7. A tape echo that wobbles." A plugin that is not filed says its kind and its description, as every row did before decision 85.

Over the list are a search box and a category menu (`HostEditor` in `shell/platform/PangeaHost.h`). The menu holds the categories of the plugins that match what is typed, by name, each with how many it holds, so a number is what choosing that category would show; it is hidden while nothing in the library is filed. A category that is chosen stays in the menu when it holds nothing, so that it can be left. The box never takes the keyboard by itself, because in a DAW the keys are the DAW's until the person presses in the box. Return gives the keys back with the search kept, and Escape also empties the box. A list with nothing in it says why, or "Nothing matches."

### The list's search

One rule for the library page on the site (`src/lib/catalogue/search.ts`), the desktop app's list (`desktop/src/lib/search.ts`) and this list (`searchRank` and its neighbours in `shell/platform/PangeaHost.h`). Each has its own copy, so a change is a change in three places. The site's tests hold the desktop copy to the site's (`src/lib/catalogue/search-copies.test.ts`), and `tools/host-check` holds the plugin's copy to the same cases.

A plugin's text is in three parts, the most telling first:

1. its category: the name, the category's other names, and those of its tags that name a kind of the category, as `compressor` does for a plugin filed under Dynamics (`categoryName`, `keywords`)
2. its own name, its tags and its kind
3. its summary, its description and its owner's name

Every word of what the person typed must start a word of that text. Each typed word counts as the number of the first part it is found in, and the plugin's rank is the sum: the lower, the better. Plugins of one rank keep the order they had. So "delay" lists the delays first, then what is named or tagged delay, then what only mentions one; "compressor" lists what is filed under Dynamics as a compressor before a filter that holds one; "tempo-synced delay" lists a delay with that tag before a gate that is tagged with both; and "eq" finds an EQ and not a sequencer.

Words: lower case; "iz" reads as "is" on both sides (equalizer, harmonizer); anything that is not a letter, a digit or a hyphen parts two words. A hyphenated word in the text counts as each of its parts and as the parts joined (lo-fi: lo, fi, lofi). Typed, it is only the parts joined. A typed word of more than three letters loses a last "s" (delays).

In this list what the plugin can open comes before what it cannot, and the rank orders each of the two.

## Panels

The loaded version's panel is drawn with the component library in a web view (`shell/platform/PangeaWebEditor.h` and `shell/platform/web/`; `pangea.js` and `pangea.css` are the library's own files, unchanged), as a built plugin's is (decision 77). **Web panel**, a switch in the top bar, is ticked in a new Pangea. Unticked, the panel is drawn by the shell's own editor (`shell/platform/PangeaEditor.h`), and that choice is saved with the session. A session saved before decision 77 opens with the library's panel whatever its switch said then, because an untouched switch was saved as off. Its footer names the member who built the plugin and is a link that opens Pangea in the system's browser: the name and the address are read from the module's `plugin.json` (`owner.name` and `site`, decision 72). Today's five looks map onto five combinations of the library (`web/host.js`), so a richer `ui.json` reaches the page as it is. A logic module's controls (a MIDI tool's) are drawn too: a menu as a choice, level bars as vertical faders, a variation as a number box, a knob as a knob; the module's display slots reach the page as "display" events, and the page lights the bar of the step playing while the transport plays (`web/mapping.js`, `playheadLighter`). The builder's MIDI tool screen has the same Library panel switch and draws through the same page. The web panel keeps its own size (a web view cannot be scaled like a picture): the page says how tall it is and the window follows. Both panels reach the parameters through `ParamSource` (`shell/platform/PangeaParamSource.h`), a plugin's own state or the host's slots.

A MIDI tool's notes reach the web panel too, for the Felt look's chord map and notes (`docs/felt-look.md`): its processor keeps the notes it sends out in a ring while a panel listens (`notes::Heard` in `shell/platform/PangeaNotes.h`, no lock, no allocation), the editor tells the page `notes: true` and sends them as a "notes" event on its timer, at most 30 times a second, and "quiet" when the tool stops playing. A plugin built for one project does the same (`HybridShell.cpp`). Checked by `tools/test-host.sh` (the page told, the notes received while it plays, "quiet" when it stops) and `tools/test-platform.sh` (the ring).

Both panels show the bypass, and both are attached to the one parameter, so the host, automation and the panel stay in step. The native panel has a switch at the far right of its title row, and draws its controls fainter while the plugin is bypassed. The web panel has the library's header bar, as the builder has: the plugin's name on the left and the bypass button at the far right. The editor hands the page `bypass: { on }` with the controls. A press calls the editor (`setBypass`), which moves the parameter as one gesture for the host, and the button changes only when the editor says where the parameter is, with a `bypass` event: after the page's own press, after a change from automation or the host, and again whenever the page says it has drawn, which it also does when it is brought back: the web view shows a blank page while the window is hidden, and what is sent then is lost. The controls are not told again: one that the host moves while the window is hidden shows its old value when the window comes back, until it moves again. That was so before the bypass and is not mended here. While the list of plugins shows in place of a panel there is no button in the window; the parameter is there for the host all the same.

The web panel shows the plugin's own meters and step lights, as the builder does (decision 91). The editor hands the page the tree of the sound code that runs (`faust: { kind, tree }`, made by `MeterTree` in `shell/platform/PangeaMeters.h`), and the page finds the meters and the step displays in it by the site's own rules (`faustMeters` in `web/mapping.js`, held to the site's by `src/lib/faust/plugin-meters.test.ts`), then draws them with the builder's `drawPanel`: every meter in the section of its Faust group, a left and right pair as one stereo meter, each at rest until a value comes. At the end of every block the audio thread copies each bar graph the page could draw into a slot of its own and counts the block (`MeterTaps`): a store each, no lock, no memory made. While the window is open, the editor's timer sends the page a `meters` event 30 times a second at most, the values by address and only when one changed; null where nothing of the plugin is heard, which is while Pangea's bypass is on and when no block has come for a quarter of a second (the host's own bypass, a track it stopped running). A null rests the meter and darkens the steps. A Faust control with `[playhead:N]` carries its `playhead` to the page, and the step display's value lights the step playing. A MIDI tool has no meters, and its bars light from its module's display slots as before. The native panel draws no meter. A plugin built for one project does all of this the same way (`HybridShell.cpp`). Checked by `tools/test-host.sh` (what each version taps, then the Step Drive Filter and an instrument in a real web view) and `tools/test-meters.sh` (two plugins built on their own).

## What the site knows of it (decision 87)

The site's build view has one filled button. On a member's own plugin it reads **Add to Pangea plugin** until the Pangea plugin has had the plugin, and **Update in Pangea plugin** for every version after. On anyone else's it is Install AU / VST3, since the Pangea plugin opens a member's own plugins only.

- **What the site keeps:** for each of the member's own projects, the newest version the desktop app has reported a module of. The app reports each module it builds or finds ready, whoever asked for it (`POST /api/desktop/host-modules`, `{ modules: [{ projectId, version }] }`), and once a session, after it signs in, the newest module of each project already in `modules/`. The site keeps a version only for the member's own project and only when it exists, and never lowers it: a song that opens an older version does not take the record back.
- **What the button does:** it sends the plugin's current version, the one the Pangea plugin plays, through `pangea://add?project=<id>&version=<n>`. The app takes the link like a request from the plugin: it builds that version's module, or finds it ready, reports it, and writes `library.json` at once, so a Pangea plugin with Follow new versions ticked swaps to it straight away and one without it has it ready for Refresh. The app's window says it is ready, or why not. The build view asks every two seconds, for two minutes, until the version is reported, then says it is ready and reads Update.
- **When it is off:** with no version, while a control is tuned, and while another version than the current one is on the stage: the Pangea plugin plays the current version, so the title says to make the one on the stage current first (Versions, Restore).
- **Not here:** the record is the member's, not a Mac's: a second Mac counts as having what the first has. Nothing is ever taken away from the record. The Pangea plugin itself did not change.

## The folder

Everything the plugin and the desktop app share is in `~/Library/Application Support/Pangea/host/`. Every file is written to a name ending `.tmp` and renamed into place, so neither side reads half a file.

### `library.json` (the app writes, the plugin reads)

```json
{
  "format": 1,
  "updatedAt": "2026-11-10T17:00:00Z",
  "appPid": 12345,
  "signedIn": true,
  "member": { "name": "Ada", "email": "ada@example.com" },
  "error": null,
  "plugins": [
    { "projectId": "…", "name": "Tape Drift", "slug": "tape-drift", "kind": "effect",
      "headVersion": 7, "mine": true, "owner": "Ada", "description": "…", "updatedAt": "…",
      "category": "delay", "categoryName": "Delay", "tags": ["tape", "dub"], "keywords": ["echo"],
      "summary": "A tape echo that wobbles. Use it for dub throws." }
  ]
}
```

`kind` is `effect`, `instrument` or `midi`. `headVersion` is `null` for a project with no version yet. The last five fields say where the plugin is filed (decision 85): the category's id and the name people read, its tags, the other words it is found by as its category (searched, never shown: the category's other names, such as "echo" for Delay, and its own tags that name a kind of the category) and the head version's own summary. They are `null`, `null`, `[]`, `[]` and `null` for a plugin that is not filed. The format number is still 1: a plugin built before decision 85 ignores the five, and a `library.json` without them, from an app built before it, reads as a list where nothing is filed. `error` says why the list could not be refreshed (the app keeps the last good `plugins`). Signed out, `signedIn` is `false` and `plugins` is empty. The app writes it once it is signed in, every 20 seconds after that, straight after a `library` request, and when the member signs in or out.

### `requests/<requestId>.json` (the plugin writes, the app reads and deletes)

```json
{ "format": 1, "requestId": "…", "type": "module", "projectId": "…", "version": null, "rebuild": false, "createdAt": "…" }
{ "format": 1, "requestId": "…", "type": "library", "createdAt": "…" }
```

`requestId` is 8 to 64 letters, digits and hyphens, and is also the file name. `version` is a number, or `null` for the project's head version, looked up fresh. `rebuild` builds again even when the module exists (the plugin asks for it when a module's format is older than it reads). The app handles one request at a time, oldest first, and ignores any other file in the folder.

### `responses/<requestId>.json` (the app writes, the plugin reads and deletes)

```json
{ "format": 1, "requestId": "…", "ok": true, "outcome": "ready", "projectId": "…", "version": 7,
  "moduleDir": "/Users/…/Pangea/host/modules/<projectId>/v7", "message": "Tape Drift v7 is ready." }
```

`outcome` is `ready`, `refused` (the checks or the policy said no: nothing was built) or `failed` (the build failed); `message` says why in plain words. For a failed build it is `build-module.sh`'s own `error`, for example "Faust 2.88.0 stopped at its time limit of 120 seconds before it had finished compiling …/host/sources/<projectId>/v4/dsp.dsp." (`shell/README.md`, "When Faust does not finish"). A `library` request is answered once `library.json` is written.

### `modules/<projectId>/v<N>/` (the app writes, through `build-module.sh`)

| File | What |
|---|---|
| `module.dylib` | The version, compiled. |
| `manifest.json` | `{ "format": 1, "abi": 2, "projectId", "version", "name", "slug", "kind", "logic", "faust", "builtAt", "sha256" }`; `sha256` is the dylib's, and `abi` is the module's number (see "The module"). |
| `plugin.json`, `ui.json` | As the project has them (`ui.json` is `{}` when there is none). |

A request for a module that already has a `manifest.json` is answered `ready` at once unless it says `rebuild`.

### What the app builds

The same as an install, from the same checks: the version is fetched, its sources and interface are checked in memory, the sources are written to `host/sources/<projectId>/v<N>/`, checked again on disk (the app's checker and `check-project.sh`), and then compiled with:

```
shell/build-module.sh <project dir> <module dir> --project-id <id> --version-number <N> [--allow-logic]
```

`--allow-logic` goes only with the member's own projects. The sources folder is the service's own, named by project id and written one request at a time: an install writes elsewhere, and two projects that share a slug never share a folder, so nothing can change the files between the checks and the compiler. An install, for its part, decides the logic policy before it writes anything, so another member's logic module never reaches the disk. For now the plugin opens only the member's own projects: a request for anyone else's is refused with "For now the Pangea plugin opens your own plugins. Install this one from the Pangea app." `build-module.sh` prints progress lines and, last, one JSON line: `{"ok": true, "stage": "done", "moduleDir", "module", "name", "slug", "kind", "logic", "abi", "faust", "seconds"}`, or `{"ok": false, "stage": "…", "error": "…"}` with exit code 1. An instrument's line also has `"effectSection"`: `"built"`, `"none"` or `"left out"` (see "The module").

## The module

A module is the Faust class and, if the project has one, the logic module, behind a C entry point (`shell/platform/PangeaModuleApi.h`). The logic module is compiled as it is into a plugin, as its own unit with the step budget's flags (`logic_native.flags`), and the module carries its own guard, so a call that runs too long or too deep is stopped inside the module exactly as in a built plugin (decision 46). Everything else is hidden, so two versions of one project can be loaded side by side during a swap.

**An instrument's effect section.** An instrument's code may define `effect = ...;` beside `process`: one stage that all its voices share, where the delay, the reverb, the output level and the safety clip usually go. The builder plays it, so the module carries it as a second Faust class (`shell/tools/build-effect.sh`, the same step a built plugin takes; `shell/README.md`, "An instrument's effect section") and answers `pangea_create_effect` with a new instance of it, or with nothing when the project has none. The plugin runs it on the mixed voices and gives its controls slots by their ids, like any other control; a control that the voice and the effect both read is one slot that sets both (decision 73). A control of the effect never shares an id with another, so never a slot: one at the address of a voice control that is another definition has `effect` after its id. The plugin does not open a module whose effect takes another number of channels than its voice gives, or gives more than the eight a plugin carries; `build-effect.sh` never makes one.

**The module's number.** `PANGEA_MODULE_ABI` in `PangeaModuleApi.h` is written into `manifest.json` (`abi`) and answered by the module itself. It changes whenever the entry points change in a way an older module would not match. The plugin loads a module only when its number is the plugin's own; for any other it writes a request with `rebuild` set, so the module is built again and never loaded wrongly. The number is 2 since the effect section (1 before): modules built before it are asked for again the first time they are opened. The plugin and the shell that builds the modules must be of one number, so after an update that changes it the plugin is built and installed again: the app's Setup says Update the Pangea plugin (or, for a developer, `shell/build-host.sh --install`). A plugin left at the old number would ask for the same module again each time it is answered.
