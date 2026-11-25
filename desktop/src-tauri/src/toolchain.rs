//! The tools the app carries, unpacked where builds can use them.
//!
//! A packaged app carries Faust and Ninja (`desktop/scripts/build-faust.sh`) as binaries in
//! `Contents/MacOS` (Tauri's `externalBin`), and beside them in `Contents/Resources/pangea/` what
//! they need: Faust's headers and libraries, the licences and Faust's source, and a
//! `manifest.json` that says what is there. A signed app must never change, and builds write
//! next to what they use, so on first start (and after each update) the app copies all of it
//! into `~/Library/Application Support/Pangea/app/<version>-<commit>/`:
//!
//! - `bin/faust`, `bin/ninja`, first on the `PATH` of every build the app starts;
//! - `include/faust` and `share/faust`, where the shell's scripts and Faust itself look for them
//!   (beside `bin/`, as in Homebrew's Faust);
//! - `licences/`.
//!
//! What the app writes there loses the quarantine flag a downloaded app's files carry, so macOS
//! never stops a tool the member already allowed when they opened the app. The copy is checked
//! before it is used: each tool must say the version the manifest names. An app without a
//! payload (a development build) carries nothing, and builds use the tools on the system path.
//!
//! `PANGEA_DESKTOP_PAYLOAD` points at a staged payload (`<dir>/pangea/` and `<dir>/bin/`, as
//! `package-mac.sh` stages it) in place of the bundle's, for tests and the scratch run, in a
//! development or test build only (`paths::OVERRIDES_HONOURED`).

use std::path::{Path, PathBuf};
use std::process::Command;

use anyhow::{bail, Context, Result};
use serde::{Deserialize, Serialize};

use crate::pins::pins;
use crate::{paths, AppState};

pub const PAYLOAD_OVERRIDE: &str = "PANGEA_DESKTOP_PAYLOAD";
const STAMP: &str = ".unpacked.json";
/// What a copy of the shell must hold to be used.
pub const SHELL_FILES: [&str; 6] =
    ["build.sh", "build-module.sh", "build-host.sh", "check-project.sh", "CMakeLists.txt", "platform/PangeaModuleApi.h"];

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct ToolStamp {
    pub version: String,
}

/// `pangea/manifest.json`, written by `package-mac.sh`.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct Manifest {
    pub format: u32,
    pub app_version: String,
    pub commit: String,
    pub faust: ToolStamp,
    pub ninja: ToolStamp,
}

impl Manifest {
    /// The folder name under `app/`: `<version>-<commit>`, letters, digits, dots and hyphens only.
    pub fn id(&self) -> String {
        format!("{}-{}", self.app_version, self.commit)
            .chars()
            .map(|c| if c.is_ascii_alphanumeric() || c == '.' || c == '-' { c } else { '-' })
            .collect()
    }
}

/// What a packaged app carries: `resources` is `.../pangea`, `bin` holds `faust` and `ninja`.
#[derive(Debug, Clone)]
pub struct Payload {
    pub resources: PathBuf,
    pub bin: PathBuf,
    pub manifest: Manifest,
}

impl Payload {
    fn load(resources: PathBuf, bin: PathBuf) -> Option<Payload> {
        let text = std::fs::read_to_string(resources.join("manifest.json")).ok()?;
        let manifest: Manifest = serde_json::from_str(&text).ok()?;
        (bin.join("faust").is_file() && bin.join("ninja").is_file()).then_some(Payload { resources, bin, manifest })
    }

    /// The staged payload named by `PANGEA_DESKTOP_PAYLOAD`, else the bundle's
    /// (`<resource dir>/pangea`, binaries beside the app's own executable), else none.
    pub fn find(resource_dir: Option<&Path>, exe_dir: Option<&Path>) -> Option<Payload> {
        if let Some(dir) = crate::paths::override_var(PAYLOAD_OVERRIDE) {
            let dir = PathBuf::from(dir);
            return Payload::load(dir.join("pangea"), dir.join("bin"));
        }
        Payload::load(resource_dir?.join("pangea"), exe_dir?.to_path_buf())
    }
}

/// The unpacked copy builds use.
#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct Toolchain {
    pub root: PathBuf,
    pub manifest: Manifest,
}

impl Toolchain {
    pub fn bin_dir(&self) -> PathBuf {
        self.root.join("bin")
    }
    pub fn licences_dir(&self) -> PathBuf {
        self.root.join("licences")
    }
    /// The app's own copy of the shell: `build.sh`, `build-module.sh`, `build-host.sh`, the
    /// platform sources and the examples, with `library/faust-versions.json` beside it, where the
    /// scripts look (`<shell>/../library`). Builds write into it (`build/`, `build-host/`).
    pub fn shell_dir(&self) -> PathBuf {
        self.root.join("shell")
    }
}

/// Copies a folder, recreating links as links. Files keep their permissions.
pub fn copy_tree(from: &Path, to: &Path) -> Result<()> {
    std::fs::create_dir_all(to).with_context(|| format!("could not create {}", to.display()))?;
    for entry in std::fs::read_dir(from).with_context(|| format!("could not read {}", from.display()))? {
        let entry = entry?;
        let kind = entry.file_type()?;
        let target = to.join(entry.file_name());
        if kind.is_symlink() {
            let link = std::fs::read_link(entry.path())?;
            std::os::unix::fs::symlink(&link, &target).with_context(|| format!("could not link {}", target.display()))?;
        } else if kind.is_dir() {
            copy_tree(&entry.path(), &target)?;
        } else {
            std::fs::copy(entry.path(), &target).with_context(|| format!("could not copy to {}", target.display()))?;
        }
    }
    Ok(())
}

/// Removes `com.apple.quarantine` from a file or everything under a folder. A copy of a file from
/// a downloaded app carries the flag; the app's own copies must not, or macOS would stop them.
pub fn strip_quarantine(path: &Path) -> Result<()> {
    let out = Command::new("/usr/bin/xattr")
        .args(["-d", "-r", "com.apple.quarantine"])
        .arg(path)
        .output()
        .context("could not run xattr")?;
    // xattr says "No such xattr" for each file without the flag; only other complaints matter.
    let said = String::from_utf8_lossy(&out.stderr);
    let real: Vec<&str> = said.lines().filter(|l| !l.trim().is_empty() && !l.contains("No such xattr")).collect();
    if !real.is_empty() {
        bail!("could not clear the quarantine flag under {}: {}", path.display(), real.join(" "));
    }
    Ok(())
}

/// True when `path` itself carries the quarantine flag.
pub fn has_quarantine(path: &Path) -> bool {
    Command::new("/usr/bin/xattr")
        .args(["-p", "com.apple.quarantine"])
        .arg(path)
        .output()
        .map(|o| o.status.success())
        .unwrap_or(false)
}

/// What a tool says about its version: `faust --version` ("FAUST Version 2.88.0"), `ninja --version`.
fn tool_version(bin: &Path) -> Option<String> {
    let out = Command::new(bin).arg("--version").env_clear().env("PATH", "/usr/bin:/bin").output().ok()?;
    let text = String::from_utf8_lossy(&out.stdout).to_string();
    crate::prereqs::parse_faust_version(&text).or_else(|| text.lines().next().map(|l| l.trim().to_string()))
}

/// Unpacks the payload into `<support>/app/<id>/` unless that copy is already there and whole,
/// checks it, and removes older copies. Returns the copy builds use.
pub fn unpack(payload: &Payload, support: &Path) -> Result<Toolchain> {
    let apps = support.join("app");
    let id = payload.manifest.id();
    let root = apps.join(&id);
    let stamp_text = serde_json::to_string_pretty(&payload.manifest)?;
    let whole = std::fs::read_to_string(root.join(STAMP)).map(|s| s == stamp_text).unwrap_or(false);
    if !whole {
        let tmp = apps.join(format!(".{id}.{}.tmp", std::process::id()));
        let _ = std::fs::remove_dir_all(&tmp);
        std::fs::create_dir_all(tmp.join("bin")).with_context(|| format!("could not create {}", tmp.display()))?;
        for tool in ["faust", "ninja"] {
            let to = tmp.join("bin").join(tool);
            std::fs::copy(payload.bin.join(tool), &to).with_context(|| format!("could not copy {tool}"))?;
            set_executable(&to)?;
        }
        copy_tree(&payload.resources.join("faust").join("include"), &tmp.join("include"))?;
        copy_tree(&payload.resources.join("faust").join("share"), &tmp.join("share"))?;
        for part in ["licences", "shell", "library"] {
            if payload.resources.join(part).is_dir() {
                copy_tree(&payload.resources.join(part), &tmp.join(part))?;
            }
        }
        strip_quarantine(&tmp)?;
        check_copy(&tmp, &payload.manifest)?;
        std::fs::write(tmp.join(STAMP), &stamp_text)?;
        if root.exists() {
            std::fs::remove_dir_all(&root).with_context(|| format!("could not replace {}", root.display()))?;
        }
        std::fs::rename(&tmp, &root).with_context(|| format!("could not move {} into place", root.display()))?;
    }
    remove_older(&apps, &id);
    Ok(Toolchain { root, manifest: payload.manifest.clone() })
}

/// Finds what the app carries, unpacks it and remembers it (`tools::apply` then puts its `bin`
/// first on every build's `PATH`). `None` when the app carries nothing (a development build).
/// It also records how it ended in `state.unpack`, which Setup waits for.
pub fn prepare(state: &AppState, resource_dir: Option<&Path>, exe_dir: Option<&Path>) -> Result<Option<Toolchain>> {
    let support = paths::support_dir();
    let payload = Payload::find(resource_dir, exe_dir);
    match support {
        Ok(support) => prepare_in(state, payload.as_ref(), &support),
        Err(e) => {
            *state.unpack.lock().unwrap() = Unpack::Failed { message: member_words(&e), details: format!("{e:#}") };
            Err(e)
        }
    }
}

/// `prepare`, with the payload found and the app's folder given (tests give a scratch one).
pub fn prepare_in(state: &AppState, payload: Option<&Payload>, support: &Path) -> Result<Option<Toolchain>> {
    *state.unpack.lock().unwrap() = Unpack::Pending;
    let result = match payload {
        Some(p) => unpack(p, support).map(Some),
        None => Ok(None),
    };
    *state.toolchain.lock().unwrap() = result.as_ref().ok().cloned().flatten();
    *state.unpack.lock().unwrap() = match &result {
        Ok(Some(_)) => Unpack::Ready,
        Ok(None) => Unpack::NoPayload,
        Err(e) => Unpack::Failed { message: member_words(e), details: format!("{e:#}") },
    };
    result
}

/// How unpacking what the app carries ended. `Pending` until it has, from the app's start.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub enum Unpack {
    #[default]
    Pending,
    Ready,
    /// The app carries nothing: a development build, or a broken copy of a packaged one.
    NoPayload,
    /// `message` in the member's words, `details` the error itself, for Copy details.
    Failed { message: String, details: String },
}

/// Waits, at most `limit`, for the unpack that starts with the app to end, and says how it did.
pub async fn wait_for_unpack(state: &AppState, limit: std::time::Duration) -> Unpack {
    let deadline = std::time::Instant::now() + limit;
    loop {
        let now = state.unpack.lock().unwrap().clone();
        if now != Unpack::Pending || std::time::Instant::now() >= deadline {
            return now;
        }
        tokio::time::sleep(std::time::Duration::from_millis(100)).await;
    }
}

/// One sentence for why the tools could not be unpacked, for a member: what went wrong on their
/// Mac, never the path or the call that failed.
pub fn member_words(e: &anyhow::Error) -> String {
    let io = e.chain().find_map(|c| c.downcast_ref::<std::io::Error>());
    let why = match io.map(|io| io.kind()) {
        Some(std::io::ErrorKind::StorageFull) => "There is not enough free space on your Mac. Free some space, then press Try again.".to_string(),
        Some(std::io::ErrorKind::PermissionDenied) => {
            "macOS did not let Pangea write to its own folder in your Library, Application Support. Press Try again; if it stops again, press Copy details for Pangea's help.".to_string()
        }
        _ => {
            let text = format!("{e:#}");
            if text.contains("carries says") || text.contains(" has no ") || text.contains("quarantine flag") {
                "This copy of Pangea is incomplete. Quit Pangea, download it again, move it to Applications and open it from there.".to_string()
            } else {
                "Something went wrong while Pangea unpacked them. Press Try again; if it stops again, press Copy details for Pangea's help.".to_string()
            }
        }
    };
    format!("Pangea could not set up the tools it carries. {why}")
}

/// What Settings' About section shows.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct About {
    pub app_version: String,
    /// The commit the app was packaged from; `None` in a development build.
    pub commit: Option<String>,
    /// Whether this build carries Faust and Ninja (a packaged app does).
    pub carries_tools: bool,
    /// The versions pinned in `desktop/tools.json`; the carried ones are the manifest's.
    pub faust: String,
    pub ninja: String,
    pub cmake: String,
    pub juce: String,
    /// Where the licences and Faust's source are on this Mac, when the app carries them.
    pub licences_dir: Option<String>,
}

pub fn about(state: &AppState) -> About {
    let t = state.toolchain.lock().unwrap().clone();
    let p = pins();
    About {
        app_version: env!("CARGO_PKG_VERSION").to_string(),
        commit: t.as_ref().map(|t| t.manifest.commit.clone()),
        carries_tools: t.is_some(),
        faust: t.as_ref().map(|t| t.manifest.faust.version.clone()).unwrap_or_else(|| p.faust.version.clone()),
        ninja: t.as_ref().map(|t| t.manifest.ninja.version.clone()).unwrap_or_else(|| p.ninja.version.clone()),
        cmake: p.cmake.version.clone(),
        juce: p.juce.version.clone(),
        licences_dir: t.as_ref().map(|t| t.licences_dir()).filter(|d| d.is_dir()).map(|d| d.display().to_string()),
    }
}

fn set_executable(path: &Path) -> Result<()> {
    use std::os::unix::fs::PermissionsExt;
    let mut perms = std::fs::metadata(path)?.permissions();
    perms.set_mode(0o755);
    std::fs::set_permissions(path, perms)?;
    Ok(())
}

/// Each tool in the copy must run and say the manifest's version, and Faust's headers and
/// libraries must be where the shell and Faust look.
fn check_copy(root: &Path, manifest: &Manifest) -> Result<()> {
    for (tool, want) in [("faust", &manifest.faust.version), ("ninja", &manifest.ninja.version)] {
        if has_quarantine(&root.join("bin").join(tool)) {
            bail!("the app's copy of {tool} still carries the quarantine flag");
        }
        let said = tool_version(&root.join("bin").join(tool));
        if said.as_deref() != Some(want.as_str()) {
            bail!("the {tool} the app carries says {}, not {want}", said.unwrap_or_else(|| "nothing".into()));
        }
    }
    for needed in ["include/faust/dsp/dsp.h", "include/faust/dsp/poly-dsp.h", "share/faust/stdfaust.lib"] {
        if !root.join(needed).is_file() {
            bail!("the app's copy of Faust has no {needed}");
        }
    }
    if root.join("shell").is_dir() {
        for needed in SHELL_FILES {
            if !root.join("shell").join(needed).is_file() {
                bail!("the app's copy of the shell has no {needed}");
            }
        }
        if !root.join("library/faust-versions.json").is_file() {
            bail!("the app's copy of the shell has no library/faust-versions.json beside it");
        }
    }
    Ok(())
}

/// Removes copies left by other versions of the app (each finished copy has a stamp; a copy
/// half made by a run that stopped is a `.tmp` folder and goes too).
fn remove_older(apps: &Path, keep: &str) {
    let Ok(entries) = std::fs::read_dir(apps) else { return };
    for e in entries.flatten() {
        let name = e.file_name().to_string_lossy().to_string();
        if name == keep || !e.path().is_dir() {
            continue;
        }
        let old_copy = e.path().join(STAMP).is_file();
        let leftover = name.starts_with('.') && name.ends_with(".tmp") && !name.contains(&format!(".{}.tmp", std::process::id()));
        if old_copy || leftover {
            let _ = std::fs::remove_dir_all(e.path());
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU32, Ordering};

    struct TempDir(PathBuf);
    impl TempDir {
        fn new(tag: &str) -> TempDir {
            static N: AtomicU32 = AtomicU32::new(0);
            let p = std::env::temp_dir().join(format!(
                "pangea-toolchain-test-{}-{}-{tag}",
                std::process::id(),
                N.fetch_add(1, Ordering::SeqCst)
            ));
            let _ = std::fs::remove_dir_all(&p);
            std::fs::create_dir_all(&p).unwrap();
            TempDir(p)
        }
    }
    impl Drop for TempDir {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.0);
        }
    }

    fn write(path: &Path, text: &str) {
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(path, text).unwrap();
    }

    fn manifest(commit: &str) -> Manifest {
        Manifest {
            format: 1,
            app_version: "0.1.0".into(),
            commit: commit.into(),
            faust: ToolStamp { version: "2.88.0".into() },
            ninja: ToolStamp { version: "1.13.2".into() },
        }
    }

    /// A payload as `package-mac.sh` stages it, with stand-ins for the two tools and the
    /// quarantine flag a downloaded app's files carry.
    fn staged(dir: &Path, m: &Manifest) -> Payload {
        let res = dir.join("pangea");
        write(&res.join("manifest.json"), &serde_json::to_string(m).unwrap());
        write(&res.join("faust/include/faust/dsp/dsp.h"), "// dsp.h\n");
        write(&res.join("faust/include/faust/dsp/poly-dsp.h"), "// poly-dsp.h\n");
        write(&res.join("faust/share/faust/stdfaust.lib"), "// stdfaust.lib\n");
        write(&res.join("licences/faust/COPYING.txt"), "LGPL\n");
        for f in SHELL_FILES {
            write(&res.join("shell").join(f), "# shell\n");
        }
        write(&res.join("shell/VERSION"), &format!("{}\n", m.commit));
        write(&res.join("library/faust-versions.json"), "{\"native\": {\"faust\": \"2.88.0\"}}\n");
        write(&dir.join("bin/faust"), &format!("#!/bin/sh\necho 'FAUST Version {}'\n", m.faust.version));
        write(&dir.join("bin/ninja"), &format!("#!/bin/sh\necho '{}'\n", m.ninja.version));
        for f in ["bin/faust", "bin/ninja", "pangea/faust/include/faust/dsp/dsp.h"] {
            let ok = Command::new("/usr/bin/xattr")
                .args(["-w", "com.apple.quarantine", "0081;6740e9a1;Safari;"])
                .arg(dir.join(f))
                .status()
                .unwrap()
                .success();
            assert!(ok, "could not set the quarantine flag on {f}");
        }
        Payload::load(res, dir.join("bin")).expect("the staged payload loads")
    }

    #[test]
    fn the_payload_is_unpacked_once_checked_and_without_quarantine() {
        let src = TempDir::new("payload");
        let support = TempDir::new("support");
        let m = manifest("abc123");
        let payload = staged(&src.0, &m);
        assert!(has_quarantine(&payload.bin.join("faust")));

        let t = unpack(&payload, &support.0).unwrap();
        assert_eq!(t.root, support.0.join("app/0.1.0-abc123"));
        assert_eq!(t.shell_dir(), t.root.join("shell"));
        for f in [
            "bin/faust",
            "bin/ninja",
            "include/faust/dsp/dsp.h",
            "share/faust/stdfaust.lib",
            "licences/faust/COPYING.txt",
            "shell/build.sh",
            "shell/platform/PangeaModuleApi.h",
            "shell/VERSION",
            "library/faust-versions.json",
        ] {
            assert!(t.root.join(f).is_file(), "{f}");
            assert!(!has_quarantine(&t.root.join(f)), "{f} still carries the quarantine flag");
        }
        assert_eq!(tool_version(&t.bin_dir().join("faust")).as_deref(), Some("2.88.0"));

        // A second start finds it whole and leaves it alone.
        let marker = t.root.join("share/faust/marker");
        std::fs::write(&marker, "x").unwrap();
        unpack(&payload, &support.0).unwrap();
        assert!(marker.is_file(), "an unpacked copy is not made again");

        // An update unpacks the new version and removes the old copy.
        let next = TempDir::new("payload-next");
        let newer = staged(&next.0, &manifest("def456"));
        let t2 = unpack(&newer, &support.0).unwrap();
        assert!(t2.root.join("bin/faust").is_file());
        assert!(!t.root.exists(), "the old copy is removed");
    }

    #[test]
    fn a_tool_that_does_not_say_its_version_is_not_used() {
        let src = TempDir::new("payload-bad");
        let support = TempDir::new("support-bad");
        let mut m = manifest("bad1");
        let payload = staged(&src.0, &m);
        m.faust.version = "2.90.19".into();
        let wrong = Payload { manifest: m, ..payload };
        let err = unpack(&wrong, &support.0).unwrap_err().to_string();
        assert!(err.contains("says 2.88.0, not 2.90.19"), "{err}");
        assert!(!support.0.join("app/0.1.0-bad1").exists(), "nothing is put in place");
    }

    #[test]
    fn a_failed_unpack_is_said_in_a_members_words() {
        let full = anyhow::Error::new(std::io::Error::from(std::io::ErrorKind::StorageFull)).context("could not copy to /x/bin/faust");
        let words = member_words(&full);
        assert!(words.starts_with("Pangea could not set up the tools it carries."), "{words}");
        assert!(words.contains("not enough free space"), "{words}");
        assert!(!words.contains("/x/bin"), "no paths for a member: {words}");
        let incomplete = anyhow::anyhow!("the faust the app carries says nothing, not 2.88.0");
        assert!(member_words(&incomplete).contains("This copy of Pangea is incomplete"));
        for w in [words, member_words(&incomplete)] {
            assert!(!w.contains('\u{2014}') && !w.contains('\u{2013}') && !w.contains('!'), "copy rules: {w}");
        }
    }

    /// Setup waits for the unpack that starts with the app, and records how it ended.
    #[tokio::test]
    async fn setup_waits_for_the_unpack_and_sees_how_it_ended() {
        let state = std::sync::Arc::new(AppState::default());
        assert_eq!(*state.unpack.lock().unwrap(), Unpack::Pending);
        assert_eq!(wait_for_unpack(&state, std::time::Duration::from_millis(150)).await, Unpack::Pending);
        let later = state.clone();
        tokio::spawn(async move {
            tokio::time::sleep(std::time::Duration::from_millis(200)).await;
            *later.unpack.lock().unwrap() = Unpack::Ready;
        });
        assert_eq!(wait_for_unpack(&state, std::time::Duration::from_secs(5)).await, Unpack::Ready);

        // A payload whose tool says the wrong version: Failed, in a member's words, with the details.
        let src = TempDir::new("payload-state");
        let support = TempDir::new("state-support");
        let m = manifest("state1");
        let mut payload = staged(&src.0, &m);
        payload.manifest.faust.version = "9.9.9".into();
        let result = prepare_in(&state, Some(&payload), &support.0);
        assert!(result.is_err());
        let ended = state.unpack.lock().unwrap().clone();
        match ended {
            Unpack::Failed { message, details } => {
                assert!(message.contains("This copy of Pangea is incomplete"), "{message}");
                assert!(details.contains("9.9.9"), "{details}");
            }
            other => panic!("expected a failure, got {other:?}"),
        }
    }

    #[test]
    fn an_app_without_a_payload_carries_nothing() {
        let empty = TempDir::new("empty");
        assert!(Payload::find(Some(&empty.0), Some(&empty.0)).is_none() || crate::paths::override_var(PAYLOAD_OVERRIDE).is_some());
        assert_eq!(manifest("a/b c").id(), "0.1.0-a-b-c");
    }
}
