# Pangea for Mac and the Pangea plugin shell

Pangea is a community where music makers build audio plugins by prompt, at https://pangea.audio. This repository holds the two parts of Pangea that run on a member's own Mac:

- **The desktop app** (`desktop/`, Tauri: Rust with a small React page). It signs in to Pangea, sets up the Mac to build plugins (Apple's build tools, Faust, Ninja, CMake, JUCE), builds the member's plugins and installs them, and installs the Pangea plugin. Start with `desktop/README.md`.
- **The plugin shell** (`shell/`, C++ on JUCE 8, with Faust). It turns a Pangea project (Faust code, sometimes a small C++ logic module, and a description of its panel) into an Audio Unit and a VST3, and it builds the Pangea plugin, which opens any of a member's plugins in a DAW. Start with `shell/README.md`.

## What is not here

The site and its builder, where plugins are made, are not in this repository, and neither is anything that runs them. The app needs a Pangea account to do anything useful: it lists, builds and installs the plugins of the member who is signed in. `desktop/mock-server` stands in for the site for development, with three fixed projects.

Comments and docs here sometimes name files that are only in Conduit AI's own repository (the site's code under `src/`, other docs under `docs/`, the MIDI shelf under `library/midi/`). They are not published.

## How this repository is made

Conduit AI publishes it as a snapshot of its own repository, at intervals, with no history: each snapshot is one commit. `SNAPSHOT` names the commit of Conduit AI's repository it came from. The paths are the same as there, so the scripts work unchanged.

## Building it

`BUILDING.md`. In short: on a Mac with Apple silicon, follow `docs/setup-a-mac.md`, "For developers". Intel Macs and Windows are not supported or tested; `BUILDING.md` says plainly how far we know a build gets.

## Licence

Conduit AI's own code here is free software under the GNU Affero General Public License, version 3 or later (`LICENSE`). Copyright (C) 2026 Conduit AI Limited.

The Felt look in `shell/platform/web` and six of the shell's examples (`shell/examples/two-hands` and the five built on the MIDI shelf) are by another author. Since 25 November 2026 he offers them, as they are here, under the GNU Affero General Public License, version 3 or later, as well as under his own licence, at your choice (`shell/platform/web/FELT-LICENSE.md`). So all the code here may be used under the GNU Affero General Public License.

JUCE is not in this repository: it is under the GNU Affero General Public License, version 3, or JUCE's own licence, and the app and the build scripts fetch it from JUCE's release. `NOTICE` and `shell/NOTICE` list every third party and its licence.

Every plugin built with the shell includes the shell and JUCE, so a plugin you build and give to others is under the GNU Affero General Public License too.
