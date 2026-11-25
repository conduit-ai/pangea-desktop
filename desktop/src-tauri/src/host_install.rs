//! The Pangea plugin (docs/pangea-plugin.md), built and installed by the app: what
//! `shell/build-host.sh --install` does in Terminal, as steps the Setup screen can show.
//!
//! 1. **Build**: `build-host.sh` in the shell builds uses (no `--install`), its lines streamed
//!    to the log; its JSON line says which stage failed, and the member gets one sentence for it.
//! 2. **Install**: the three Audio Units and two VST3s are copied with `ditto` into
//!    `~/Library/Audio/Plug-Ins` (`Components`, `VST3`), each replacing the one before, and the
//!    copies are cleared of the quarantine flag. A stamp (`host-plugin.json` in the app's folder)
//!    records the plugin's version and module number, so the app can tell when the shell it now
//!    has needs a newer plugin.
//! 3. **The registrar**: `AudioComponentRegistrar` is restarted so DAWs see the new Audio Units.
//! 4. **Apple's check**: `auval -v` on all three (`aufx Phfx`, `aumu Phin`, `aumi Phmi`, maker
//!    `Pnga`), which loads each in Apple's own host. The app runs in the member's GUI session,
//!    where the per-user registrar lives, so no `launchctl` is needed.
//!
//! With `PANGEA_DESKTOP_HOME` set (a scratch run), steps 3 and 4 are left out and say so: the
//! registrar and `auval` belong to the real Mac and would see the real plug-in folders.
//!
//! A DAW that is open while the plugin is replaced may hold the old one, so the app looks for
//! one first (`running_daws`) and asks the member to close it.

use std::ffi::OsString;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::Instant;

use anyhow::{Context, Result};
use serde::{Deserialize, Serialize};
use serde_json::Value;

use crate::build::{self, JobLog};
use crate::paths;
use crate::toolchain::strip_quarantine;

pub const MANUFACTURER: &str = "Pnga";
/// The events an install sends the page: each line of its log, and each stage as it starts.
pub const LOG_EVENT: &str = "host-install-log";
pub const STAGE_EVENT: &str = "host-install-stage";

/// The plugin's bundles, where `build-host.sh` builds them (under its `build-host/`) and their
/// names in the plug-in folders. Audio Units carry their type and code for `auval`.
pub struct HostBundle {
    pub built: &'static str,
    pub name: &'static str,
    pub au: Option<(&'static str, &'static str)>,
}

pub const BUNDLES: [HostBundle; 5] = [
    HostBundle { built: "PangeaHostEffect_artefacts/Release/AU/Pangea.component", name: "Pangea.component", au: Some(("aufx", "Phfx")) },
    HostBundle {
        built: "PangeaHostInstrument_artefacts/Release/AU/Pangea Instrument.component",
        name: "Pangea Instrument.component",
        au: Some(("aumu", "Phin")),
    },
    HostBundle {
        built: "PangeaHostMidiFx_artefacts/Release/AU/Pangea MIDI FX.component",
        name: "Pangea MIDI FX.component",
        au: Some(("aumi", "Phmi")),
    },
    HostBundle { built: "PangeaHostEffect_artefacts/Release/VST3/Pangea.vst3", name: "Pangea.vst3", au: None },
    HostBundle { built: "PangeaHostInstrument_artefacts/Release/VST3/Pangea Instrument.vst3", name: "Pangea Instrument.vst3", au: None },
];

pub const RESCAN_MESSAGE: &str = "The Pangea plugin is installed. Open your DAW and add Pangea to a track: Pangea for an effect, Pangea Instrument for an instrument or a MIDI tool. Logic Pro finds it when it next starts; if it does not, open Logic Pro, Settings, Plug-in Manager, and press Reset and Rescan Selection. In Ableton Live, open Settings, Plug-Ins, turn on Audio Units or VST3 Plug-In System Folders, and press Rescan.";

fn folder_for(name: &str) -> &'static str {
    if name.ends_with(".vst3") {
        "VST3"
    } else {
        "Components"
    }
}

/// Where a bundle is installed.
pub fn installed_path(plugins: &Path, b: &HostBundle) -> PathBuf {
    plugins.join(folder_for(b.name)).join(b.name)
}

/// `HOST_VERSION` in the shell's `build-host.sh`: the version a DAW sees.
pub fn host_version(shell_dir: &Path) -> Option<String> {
    let text = std::fs::read_to_string(shell_dir.join("build-host.sh")).ok()?;
    text.lines().find_map(|l| l.strip_prefix("HOST_VERSION=\"").and_then(|r| r.split('"').next()).map(str::to_string))
}

/// `PANGEA_MODULE_ABI`, the module number the plugin and the shell must share.
pub fn shell_abi(shell_dir: &Path) -> Option<u32> {
    let text = std::fs::read_to_string(shell_dir.join("platform/PangeaModuleApi.h")).ok()?;
    text.lines().find_map(|l| l.strip_prefix("#define PANGEA_MODULE_ABI ")?.split_whitespace().next()?.parse().ok())
}

/// `CFBundleShortVersionString` of an installed bundle.
pub fn bundle_version(bundle: &Path) -> Option<String> {
    let out = Command::new("/usr/bin/plutil")
        .args(["-extract", "CFBundleShortVersionString", "raw", "-o", "-"])
        .arg(bundle.join("Contents/Info.plist"))
        .output()
        .ok()?;
    out.status.success().then(|| String::from_utf8_lossy(&out.stdout).trim().to_string()).filter(|v| !v.is_empty())
}

/// What the app wrote when it last installed the plugin.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct Stamp {
    pub host_version: String,
    pub abi: u32,
    /// The shell's `VERSION` (the commit the app was packaged from), when it had one.
    pub shell: Option<String>,
    pub app_version: String,
    pub installed_at: String,
    /// Apple's check of the three Audio Units: `passed`, or `skipped in a scratch run`. A stamp
    /// is written only after one of the two.
    #[serde(default)]
    pub validation: String,
}

pub fn stamp_path(support: &Path) -> PathBuf {
    support.join("host-plugin.json")
}

fn read_stamp(support: &Path) -> Option<Stamp> {
    serde_json::from_str(&std::fs::read_to_string(stamp_path(support)).ok()?).ok()
}

fn shell_version(shell_dir: &Path) -> Option<String> {
    std::fs::read_to_string(shell_dir.join("VERSION")).ok().map(|v| v.trim().to_string()).filter(|v| !v.is_empty())
}

/// Whether the Pangea plugin is on this Mac and right for the shell builds use.
#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct HostPlugin {
    /// `missing`, `partial` (some bundles), `installed`, `check-failed` (Apple's check of the last
    /// install did not pass), `update-needed` (its version or module
    /// number is not the shell's: modules would not load) or `update-available` (made from an
    /// older Pangea, but it works).
    pub state: String,
    pub message: String,
    pub installed_version: Option<String>,
    pub wanted_version: Option<String>,
    /// True when the app installed it (a stamp); false for one installed in Terminal.
    pub by_app: bool,
}

/// Looks at the plug-in folders, the shell and the stamp. Reads files only.
pub fn detect(plugins: &Path, shell_dir: &Path, support: &Path) -> HostPlugin {
    let present = BUNDLES.iter().filter(|b| installed_path(plugins, b).join("Contents").is_dir()).count();
    let installed_version = bundle_version(&installed_path(plugins, &BUNDLES[0]));
    let wanted_version = host_version(shell_dir);
    let stamp = read_stamp(support);
    let make = |state: &str, message: String| HostPlugin {
        state: state.into(),
        message,
        installed_version: installed_version.clone(),
        wanted_version: wanted_version.clone(),
        by_app: stamp.is_some(),
    };
    if present == 0 {
        return make("missing", "The Pangea plugin is the one plugin you add in your DAW. It opens every plugin you make. Install it once; it takes a few minutes the first time.".into());
    }
    if present < BUNDLES.len() {
        return make("partial", format!("{present} of the Pangea plugin's 5 parts are installed. Install it again to put all of them in place."));
    }
    // A plugin the app installed at the shell's module number opens every module the shell builds,
    // whatever its version: then a newer version is offered, not demanded (0.3.0 only added a
    // sidechain input). Installed in Terminal, with no stamp, its module number is not known.
    let same_abi = stamp.as_ref().is_some_and(|s| Some(s.abi) == shell_abi(shell_dir));
    if let (Some(have), Some(want)) = (&installed_version, &wanted_version) {
        if have != want && !same_abi {
            return make("update-needed", format!("The Pangea plugin here is version {have}; this Pangea needs {want}. Update it so it can open your plugins."));
        }
    }
    if failed_path(support).is_file() {
        return make("check-failed", CHECK_FAILED_MESSAGE.into());
    }
    match &stamp {
        Some(s) if Some(s.abi) != shell_abi(shell_dir) => {
            make("update-needed", "This Pangea builds plugins the installed Pangea plugin cannot open. Update the Pangea plugin.".into())
        }
        Some(s) if s.shell.is_some() && s.shell != shell_version(shell_dir) => {
            make("update-available", "The Pangea plugin works. A newer one came with this version of Pangea: Update the Pangea plugin when your DAW is closed.".into())
        }
        Some(_) if installed_version != wanted_version => {
            make("update-available", "The Pangea plugin works. A newer one came with this version of Pangea: Update the Pangea plugin when your DAW is closed.".into())
        }
        Some(_) => make("installed", "The Pangea plugin is installed.".into()),
        None => make("installed", "The Pangea plugin is installed (it was built outside the app).".into()),
    }
}

/// The DAWs a running process list shows, by name. `ps` gives each process's executable path,
/// which holds the app's bundle name.
pub fn daws_in(ps: &str) -> Vec<String> {
    const KNOWN: [(&str, &str); 9] = [
        ("Logic Pro", "Logic Pro"),
        ("Ableton Live", "Ableton Live"),
        ("Bitwig Studio", "Bitwig Studio"),
        ("GarageBand", "GarageBand"),
        ("REAPER", "REAPER"),
        ("Cubase", "Cubase"),
        ("Studio One", "Studio One"),
        ("FL Studio", "FL Studio"),
        ("MainStage", "MainStage"),
    ];
    let mut found = Vec::new();
    for line in ps.lines() {
        let Some(app) = line.split(".app/Contents/MacOS/").next().filter(|_| line.contains(".app/Contents/MacOS/")) else { continue };
        let bundle = app.rsplit('/').next().unwrap_or("");
        for (prefix, name) in KNOWN {
            if bundle.starts_with(prefix) && !found.iter().any(|f: &String| f == name) {
                found.push(name.to_string());
            }
        }
    }
    found
}

/// The DAWs open now.
pub fn running_daws() -> Vec<String> {
    match Command::new("/bin/ps").args(["-axo", "comm="]).output() {
        Ok(o) => daws_in(&String::from_utf8_lossy(&o.stdout)),
        Err(_) => Vec::new(),
    }
}

/// One plain sentence for a build that stopped, by the stage `build-host.sh` names. Never the
/// script's own words: those are for Terminal ("brew install cmake", "git clone ... JUCE") and
/// go to the log, which Copy details carries.
pub fn failure_message(stage: &str, _error: &str) -> String {
    match stage {
        "tools" => TOOLS_FIRST_MESSAGE.into(),
        "configure" => "CMake could not prepare the Pangea plugin's build. Press Try again; if it stops again, press Copy details for Pangea's help.".into(),
        "build" => "The Pangea plugin did not compile. Press Try again; if it stops again, press Copy details for Pangea's help.".into(),
        _ => "The Pangea plugin's build stopped. Press Try again; if it stops again, press Copy details for Pangea's help.".into(),
    }
}

pub const TOOLS_FIRST_MESSAGE: &str = "A tool the Pangea plugin needs is not ready yet. Open Setup and press Get Pangea's tools, then install the Pangea plugin again.";

/// The answer before anything is built when Pangea's tools are not all there yet: the install
/// would only stop at `build-host.sh`'s first check, in Terminal's words.
pub fn tools_first(needs: &crate::tools::Needs, has_shell: bool) -> Option<InstallReport> {
    (!has_shell || !needs.nothing() || needs.juce_in_own_shell_missing).then(|| InstallReport {
        ok: false,
        stage: "tools".into(),
        message: TOOLS_FIRST_MESSAGE.into(),
        auval: Vec::new(),
        seconds: 0.0,
    })
}

/// What an install did.
#[derive(Debug, Clone, Serialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct InstallReport {
    pub ok: bool,
    /// `build`, `install`, `registrar`, `auval` or `done`: where it ended.
    pub stage: String,
    pub message: String,
    /// `auval` per Audio Unit: `passed`, `failed` or `skipped`.
    pub auval: Vec<(String, String)>,
    pub seconds: f64,
}

/// The answer when a DAW is open: nothing was built or installed yet.
pub fn close_daws_first(daws: &[String]) -> InstallReport {
    let names = match daws {
        [one] => one.clone(),
        [rest @ .., last] => format!("{} and {last}", rest.join(", ")),
        [] => "your DAW".into(),
    };
    InstallReport {
        ok: false,
        stage: "daw".into(),
        message: format!("Close {names} first, then press Install again. The plugin is replaced while the install runs, and an open DAW may hold the old one."),
        auval: Vec::new(),
        seconds: 0.0,
    }
}

/// Each stage as it starts, for the page.
pub trait Stages: Send + Sync {
    fn stage(&self, stage: &str);
}

/// Puts the bundle `from` at `to` and never leaves neither: the new one is copied beside the old
/// under a hidden name (which no DAW scans), cleared of the quarantine flag, the old one is moved
/// aside, the new one renamed into place, and only then the old one removed. A copy that fails
/// leaves the old bundle where it was; a rename that fails puts it back.
pub fn replace_bundle(from: &Path, to: &Path) -> Result<()> {
    let folder = to.parent().context("no plug-in folder")?;
    let name = to.file_name().context("no bundle name")?.to_string_lossy().to_string();
    std::fs::create_dir_all(folder).with_context(|| format!("could not create {}", folder.display()))?;
    let new = folder.join(format!(".{name}.pangea-new"));
    let old = folder.join(format!(".{name}.pangea-old"));
    for leftover in [&new, &old] {
        if leftover.exists() {
            std::fs::remove_dir_all(leftover).with_context(|| format!("could not remove {}", leftover.display()))?;
        }
    }
    if !from.is_dir() {
        anyhow::bail!("{} is not there to install", from.display());
    }
    let out = Command::new("/usr/bin/ditto").arg(from).arg(&new).output().context("could not run ditto")?;
    if !out.status.success() {
        let _ = std::fs::remove_dir_all(&new);
        anyhow::bail!("ditto: {}", String::from_utf8_lossy(&out.stderr).trim());
    }
    strip_quarantine(&new)?;
    let had_old = to.exists();
    if had_old {
        std::fs::rename(to, &old).with_context(|| format!("could not move {} aside", to.display()))?;
    }
    if let Err(e) = std::fs::rename(&new, to) {
        if had_old {
            let _ = std::fs::rename(&old, to);
        }
        let _ = std::fs::remove_dir_all(&new);
        return Err(e).with_context(|| format!("could not put {} in place", to.display()));
    }
    if had_old {
        let _ = std::fs::remove_dir_all(&old);
    }
    Ok(())
}

/// The two steps that act on the Mac's own Audio Unit registry, behind a trait so tests can make
/// Apple's check pass or fail.
pub trait System: Send + Sync {
    /// True in a scratch run (`paths::is_scratch_home`): both steps are left out, and say so.
    fn scratch(&self) -> bool;
    fn restart_registrar(&self);
    /// `auval -v <kind> <code> Pnga`: whether it said AU VALIDATION SUCCEEDED, and what it said.
    fn auval(&self, kind: &str, code: &str) -> (bool, String);
}

/// The real Mac.
pub struct RealSystem;

impl System for RealSystem {
    fn scratch(&self) -> bool {
        paths::is_scratch_home()
    }
    fn restart_registrar(&self) {
        let _ = Command::new("/usr/bin/killall").args(["-9", "AudioComponentRegistrar"]).output();
        std::thread::sleep(std::time::Duration::from_secs(1));
    }
    fn auval(&self, kind: &str, code: &str) -> (bool, String) {
        match Command::new("/usr/bin/auval").args(["-v", kind, code, MANUFACTURER]).output() {
            Ok(out) => {
                let text = String::from_utf8_lossy(&out.stdout).to_string() + &String::from_utf8_lossy(&out.stderr);
                (text.contains("AU VALIDATION SUCCEEDED"), text)
            }
            Err(e) => (false, format!("could not run auval: {e}")),
        }
    }
}

/// Where the app notes that Apple's check of the plugin it installed did not pass, so the plugin
/// is not called installed after the app starts again. Removed by the next install that passes.
pub fn failed_path(support: &Path) -> PathBuf {
    support.join("host-plugin.check-failed.json")
}

pub const CHECK_FAILED_MESSAGE: &str = "Apple's check of the Pangea plugin did not pass, so your DAW may not list it. Install it again; if it stops again, press Copy details for Pangea's help.";

/// Builds and installs the Pangea plugin from `shell_dir` into `plugins`, and checks it.
pub async fn install(shell_dir: &Path, plugins: &Path, support: &Path, log: &dyn JobLog, stages: &dyn Stages) -> InstallReport {
    install_with(shell_dir, plugins, support, log, stages, &RealSystem).await
}

/// `install`, with the registrar and `auval` given (tests make them fail).
pub async fn install_with(
    shell_dir: &Path,
    plugins: &Path,
    support: &Path,
    log: &dyn JobLog,
    stages: &dyn Stages,
    system: &dyn System,
) -> InstallReport {
    let started = Instant::now();
    let mut report = InstallReport { ok: false, stage: "build".into(), message: String::new(), auval: Vec::new(), seconds: 0.0 };
    let result = run(shell_dir, plugins, support, log, stages, system, &mut report).await;
    if let Err(e) = result {
        if report.message.is_empty() {
            report.message = "The Pangea plugin could not be installed. Press Try again; if it stops again, press Copy details for Pangea's help.".into();
        }
        log.app(&format!("error: {e:#}"));
    }
    report.seconds = started.elapsed().as_secs_f64();
    report
}

async fn run(
    shell_dir: &Path,
    plugins: &Path,
    support: &Path,
    log: &dyn JobLog,
    stages: &dyn Stages,
    system: &dyn System,
    report: &mut InstallReport,
) -> Result<()> {
    // 1. Build
    stages.stage("build");
    let script = shell_dir.join("build-host.sh");
    if !script.is_file() {
        report.message = "Pangea has no shell to build the plugin with. Open Setup, Pangea's tools.".into();
        anyhow::bail!("no {}", script.display());
    }
    log.app(&format!("$ bash {}", script.display()));
    let (status_ok, summary) = build::run_script(&script, &Vec::<OsString>::new(), shell_dir, log).await?;
    let ok = status_ok && summary.as_ref().and_then(|s| s.get("ok")).and_then(Value::as_bool).unwrap_or(false);
    if !ok {
        let stage = summary.as_ref().and_then(|s| s.get("stage")).and_then(Value::as_str).unwrap_or("build");
        let error = summary.as_ref().and_then(|s| s.get("error")).and_then(Value::as_str).unwrap_or("");
        report.message = failure_message(stage, error);
        anyhow::bail!("build-host.sh stopped at {stage}: {error}");
    }
    let built = shell_dir.join("build-host");
    for b in &BUNDLES {
        if !built.join(b.built).is_dir() {
            report.message = "The Pangea plugin was built, but a part of it is missing. Press Try again.".into();
            anyhow::bail!("built, but {} is missing", built.join(b.built).display());
        }
    }

    // 2. Install
    report.stage = "install".into();
    stages.stage("install");
    for b in &BUNDLES {
        let to = installed_path(plugins, b);
        if let Err(e) = replace_bundle(&built.join(b.built), &to) {
            report.message = format!("Pangea could not copy {} into your plug-in folder. The one there before is still there.", b.name);
            return Err(e);
        }
        log.app(&format!("installed {}", to.display()));
    }
    // The bundles on disk are new and not yet checked: until Apple's check passes, no stamp
    // says they are installed.
    std::fs::create_dir_all(support)?;
    let _ = std::fs::remove_file(stamp_path(support));

    // 3. The registrar, and 4. Apple's check, on the real Mac only
    let scratch = system.scratch();
    report.stage = "registrar".into();
    stages.stage("registrar");
    if scratch {
        log.app("left out in a scratch run: restarting AudioComponentRegistrar (it serves the real plug-in folders)");
    } else {
        system.restart_registrar();
    }
    report.stage = "auval".into();
    stages.stage("auval");
    for b in BUNDLES.iter() {
        let Some((kind, code)) = b.au else { continue };
        let spec = format!("{kind} {code} {MANUFACTURER}");
        if scratch {
            log.app(&format!("left out in a scratch run: auval -v {spec} (it would check the real plug-in folders)"));
            report.auval.push((spec, "skipped".into()));
            continue;
        }
        let (passed, text) = system.auval(kind, code);
        log.app(&format!("auval -v {spec}: {}", if passed { "AU VALIDATION SUCCEEDED" } else { "failed" }));
        report.auval.push((spec.clone(), if passed { "passed" } else { "failed" }.into()));
        if !passed {
            for l in text.lines().rev().take(15).collect::<Vec<_>>().into_iter().rev() {
                log.push("stderr", l);
            }
            report.message = format!(
                "The Pangea plugin is in your plug-in folders, but Apple's check of {} did not pass, so your DAW may not list it. Press Try again; if it stops again, press Copy details for Pangea's help.",
                b.name.trim_end_matches(".component")
            );
            let note = serde_json::json!({ "message": CHECK_FAILED_MESSAGE, "auval": spec, "at": chrono::Utc::now().to_rfc3339_opts(chrono::SecondsFormat::Secs, true) });
            std::fs::write(failed_path(support), serde_json::to_string_pretty(&note)? + "\n")?;
            anyhow::bail!("auval -v {spec} did not succeed");
        }
    }

    // The stamp, only now: Apple's check passed, or was left out in a scratch run, and it says which.
    let stamp = Stamp {
        host_version: host_version(shell_dir).unwrap_or_default(),
        abi: shell_abi(shell_dir).unwrap_or(0),
        shell: shell_version(shell_dir),
        app_version: env!("CARGO_PKG_VERSION").into(),
        installed_at: chrono::Utc::now().to_rfc3339_opts(chrono::SecondsFormat::Secs, true),
        validation: if scratch { "skipped in a scratch run".into() } else { "passed".into() },
    };
    std::fs::write(stamp_path(support), serde_json::to_string_pretty(&stamp)? + "\n")?;
    let _ = std::fs::remove_file(failed_path(support));
    report.ok = true;
    report.stage = "done".into();
    report.message = RESCAN_MESSAGE.into();
    stages.stage("done");
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU32, Ordering};

    const SHELL_DIR: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../../shell");

    struct TempDir(PathBuf);
    impl TempDir {
        fn new(tag: &str) -> TempDir {
            static N: AtomicU32 = AtomicU32::new(0);
            let p = std::env::temp_dir().join(format!("pangea-host-install-{}-{}-{tag}", std::process::id(), N.fetch_add(1, Ordering::SeqCst)));
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

    /// A stand-in shell: the files `detect` reads, at the versions given.
    fn shell(dir: &Path, host: &str, abi: u32, version: &str) {
        std::fs::create_dir_all(dir.join("platform")).unwrap();
        std::fs::write(dir.join("build-host.sh"), format!("#!/bin/bash\nHOST_VERSION=\"{host}\"\n")).unwrap();
        std::fs::write(dir.join("platform/PangeaModuleApi.h"), format!("#define PANGEA_MODULE_ABI {abi}\n")).unwrap();
        std::fs::write(dir.join("VERSION"), format!("{version}\n")).unwrap();
    }

    fn bundle(plugins: &Path, b: &HostBundle, version: &str) {
        let contents = installed_path(plugins, b).join("Contents");
        std::fs::create_dir_all(&contents).unwrap();
        let plist = format!(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\"><dict><key>CFBundleShortVersionString</key><string>{version}</string></dict></plist>\n"
        );
        std::fs::write(contents.join("Info.plist"), plist).unwrap();
    }

    #[test]
    fn the_bundles_and_codes_are_build_host_shs() {
        let script = std::fs::read_to_string(Path::new(SHELL_DIR).join("build-host.sh")).unwrap();
        for b in &BUNDLES {
            let file = b.built.rsplit('/').next().unwrap();
            assert!(script.contains(file), "build-host.sh does not name {file}");
            assert!(script.contains(b.built.split('/').next().unwrap()), "{}", b.built);
            if let Some((kind, code)) = b.au {
                assert!(script.contains(&format!("{kind} {code}")), "build-host.sh does not check {kind} {code}");
            }
        }
        assert!(script.contains(&format!("{MANUFACTURER} >")) || script.contains(&format!("$spec {MANUFACTURER}")));
        assert_eq!(host_version(Path::new(SHELL_DIR)).as_deref(), Some("0.3.0"));
        assert_eq!(shell_abi(Path::new(SHELL_DIR)), Some(2));
    }

    #[test]
    fn the_plugin_is_told_missing_installed_or_out_of_date() {
        let t = TempDir::new("detect");
        let (plugins, shell_dir, support) = (t.0.join("Plug-Ins"), t.0.join("shell"), t.0.join("support"));
        shell(&shell_dir, "0.2.0", 2, "aaa111");
        assert_eq!(detect(&plugins, &shell_dir, &support).state, "missing");

        bundle(&plugins, &BUNDLES[0], "0.2.0");
        assert_eq!(detect(&plugins, &shell_dir, &support).state, "partial");

        // All five, installed in Terminal at the shell's version: ticked, nothing to reinstall.
        for b in &BUNDLES[1..] {
            bundle(&plugins, b, "0.2.0");
        }
        let by_hand = detect(&plugins, &shell_dir, &support);
        assert_eq!((by_hand.state.as_str(), by_hand.by_app), ("installed", false));
        assert_eq!(by_hand.installed_version.as_deref(), Some("0.2.0"));

        // An older plugin than the shell needs.
        shell(&shell_dir, "0.3.0", 2, "aaa111");
        assert_eq!(detect(&plugins, &shell_dir, &support).state, "update-needed");
        shell(&shell_dir, "0.2.0", 2, "aaa111");

        // Installed by the app: the module number must match, a newer shell offers an update.
        let stamp = Stamp {
            host_version: "0.2.0".into(),
            abi: 2,
            shell: Some("aaa111".into()),
            app_version: "0.1.0".into(),
            installed_at: "2026-11-19T00:00:00Z".into(),
            validation: "passed".into(),
        };
        std::fs::create_dir_all(&support).unwrap();
        std::fs::write(stamp_path(&support), serde_json::to_string(&stamp).unwrap()).unwrap();
        assert_eq!(detect(&plugins, &shell_dir, &support).state, "installed");
        shell(&shell_dir, "0.2.0", 2, "bbb222");
        assert_eq!(detect(&plugins, &shell_dir, &support).state, "update-available");
        shell(&shell_dir, "0.2.0", 3, "bbb222");
        assert_eq!(detect(&plugins, &shell_dir, &support).state, "update-needed");

        // Only the plugin's version differs (0.3.0 gave Pangea a sidechain input): the installed
        // one, put there by the app at the shell's module number, still opens every plugin, so
        // the update is offered, not demanded.
        shell(&shell_dir, "0.3.0", 2, "aaa111");
        let newer = detect(&plugins, &shell_dir, &support);
        assert_eq!(newer.state, "update-available");
        assert!(newer.message.starts_with("The Pangea plugin works."), "{}", newer.message);
        // With another module number it cannot, and says so.
        shell(&shell_dir, "0.3.0", 3, "aaa111");
        assert_eq!(detect(&plugins, &shell_dir, &support).state, "update-needed");
        // Installed in Terminal, with no stamp, its module number is unknown: as before.
        std::fs::remove_file(stamp_path(&support)).unwrap();
        shell(&shell_dir, "0.3.0", 2, "aaa111");
        assert_eq!(detect(&plugins, &shell_dir, &support).state, "update-needed");
    }

    #[test]
    fn open_daws_are_found_by_their_app() {
        let ps = "/sbin/launchd\n/Applications/Logic Pro.app/Contents/MacOS/Logic Pro\n/Applications/Ableton Live 12 Suite.app/Contents/MacOS/Live\n/Applications/Logic Pro.app/Contents/XPCServices/x.xpc/Contents/MacOS/x\n/Applications/Bitwig Studio.app/Contents/MacOS/BitwigStudio\n/Applications/Pangea.app/Contents/MacOS/pangea-desktop\n";
        assert_eq!(daws_in(ps), vec!["Logic Pro", "Ableton Live", "Bitwig Studio"]);
        assert!(daws_in("/usr/libexec/logd\n").is_empty());
        let r = close_daws_first(&daws_in(ps));
        assert_eq!((r.ok, r.stage.as_str()), (false, "daw"));
        assert!(r.message.starts_with("Close Logic Pro, Ableton Live and Bitwig Studio first, then press Install again."), "{}", r.message);
        assert!(close_daws_first(&["Logic Pro".to_string()]).message.starts_with("Close Logic Pro first"));
    }

    #[test]
    fn the_messages_follow_the_copy_rules() {
        for text in [
            RESCAN_MESSAGE.to_string(),
            failure_message("configure", ""),
            failure_message("build", ""),
            failure_message("tools", "cmake not found"),
        ] {
            assert!(!text.contains('\u{2014}') && !text.contains('\u{2013}') && !text.contains('!'), "copy rules: {text}");
        }
    }

    struct Quiet;
    impl JobLog for Quiet {
        fn push(&self, _stream: &str, _line: &str) {}
    }
    impl Stages for Quiet {
        fn stage(&self, _stage: &str) {}
    }

    /// A stand-in `build-host.sh` that ends as the real one does when CMake cannot configure.
    #[tokio::test]
    async fn a_build_that_stops_says_which_stage_in_one_sentence() {
        let t = TempDir::new("fail");
        let shell_dir = t.0.join("shell");
        std::fs::create_dir_all(&shell_dir).unwrap();
        std::fs::write(
            shell_dir.join("build-host.sh"),
            "#!/bin/bash\necho 'configuring'\necho '{\"ok\": false, \"stage\": \"configure\", \"installed\": false, \"auval\": \"skipped\", \"check\": \"skipped\", \"seconds\": 1, \"error\": \"cmake configure failed\"}'\nexit 1\n",
        )
        .unwrap();
        let r = install(&shell_dir, &t.0.join("Plug-Ins"), &t.0.join("support"), &Quiet, &Quiet).await;
        assert!(!r.ok);
        assert_eq!(r.stage, "build");
        assert_eq!(r.message, failure_message("configure", ""));
        assert!(!t.0.join("Plug-Ins").exists(), "nothing is installed");
    }

    /// Replacing a bundle never leaves the plug-in folder without one.
    #[test]
    fn replacing_a_bundle_never_leaves_none() {
        let t = TempDir::new("replace");
        let to = t.0.join("Components/Pangea.component");
        std::fs::create_dir_all(to.join("Contents/MacOS")).unwrap();
        std::fs::write(to.join("Contents/MacOS/Pangea"), "old").unwrap();

        // A copy that cannot be made: the old bundle is still there, whole.
        let err = replace_bundle(&t.0.join("built/missing.component"), &to);
        assert!(err.is_err());
        assert_eq!(std::fs::read_to_string(to.join("Contents/MacOS/Pangea")).unwrap(), "old");

        // A good one: in place, nothing left beside it.
        let from = t.0.join("built/Pangea.component");
        std::fs::create_dir_all(from.join("Contents/MacOS")).unwrap();
        std::fs::write(from.join("Contents/MacOS/Pangea"), "new").unwrap();
        replace_bundle(&from, &to).unwrap();
        assert_eq!(std::fs::read_to_string(to.join("Contents/MacOS/Pangea")).unwrap(), "new");
        let names: Vec<String> = std::fs::read_dir(t.0.join("Components")).unwrap().flatten().map(|e| e.file_name().to_string_lossy().to_string()).collect();
        assert_eq!(names, vec!["Pangea.component".to_string()]);

        // And where there was none before.
        let fresh = t.0.join("VST3/Pangea.vst3");
        replace_bundle(&from, &fresh).unwrap();
        assert!(fresh.join("Contents/MacOS/Pangea").is_file());
    }

    /// `build-host.sh` speaks to someone in Terminal; the member never reads its lines.
    #[tokio::test]
    async fn no_terminal_words_reach_a_member() {
        let terminal = [
            "cmake not found. Install it with: brew install cmake",
            "JUCE is missing. Run: git clone --depth 1 --branch 8.0.10 https://github.com/juce-framework/JUCE /x/shell/JUCE",
        ];
        let said = |m: &str| ["brew", "git clone", "Run:", "Terminal", "xcode-select"].iter().any(|w| m.contains(w));
        for stage in ["tools", "configure", "build", "faust", "check", "anything"] {
            for e in terminal {
                let m = failure_message(stage, e);
                assert!(!said(&m), "{stage}: {m}");
            }
        }

        // The real path: build-host.sh stops at its tools check with Homebrew's words.
        let t = TempDir::new("terminal-words");
        let shell_dir = t.0.join("shell");
        std::fs::create_dir_all(&shell_dir).unwrap();
        std::fs::write(
            shell_dir.join("build-host.sh"),
            "#!/bin/bash\necho 'error: cmake not found. Install it with: brew install cmake' >&2\necho '{\"ok\": false, \"stage\": \"tools\", \"installed\": false, \"auval\": \"skipped\", \"check\": \"skipped\", \"seconds\": 0, \"error\": \"cmake not found. Install it with: brew install cmake\"}'\nexit 1\n",
        )
        .unwrap();
        let r = install(&shell_dir, &t.0.join("Plug-Ins"), &t.0.join("support"), &Quiet, &Quiet).await;
        assert!(!r.ok);
        assert_eq!(r.message, TOOLS_FIRST_MESSAGE);
        assert!(!said(&r.message));

        // Settings' Install waits for Pangea's tools rather than running into that check.
        let ready = crate::tools::Needs { cmake: false, juce: false, juce_in_own_shell_missing: false };
        assert!(tools_first(&ready, true).is_none());
        let first = tools_first(&crate::tools::Needs { cmake: true, ..ready.clone() }, true).expect("tools first");
        assert_eq!((first.stage.as_str(), first.message.as_str()), ("tools", TOOLS_FIRST_MESSAGE));
        assert!(tools_first(&ready, false).is_some(), "no shell, no install");
        assert!(tools_first(&crate::tools::Needs { juce_in_own_shell_missing: true, ..ready }, true).is_some());
    }

    /// A stand-in shell whose `build-host.sh` builds the five bundles at version 0.2.0.
    fn building_shell(dir: &Path) {
        shell(dir, "0.2.0", 2, "aaa111");
        let mut script = String::from("#!/bin/bash\nHOST_VERSION=\"0.2.0\"\ncd \"$(dirname \"$0\")\"\n");
        for b in &BUNDLES {
            script.push_str(&format!(
                "mkdir -p \"build-host/{0}/Contents/MacOS\"\nprintf '<?xml version=\"1.0\"?><plist version=\"1.0\"><dict><key>CFBundleShortVersionString</key><string>0.2.0</string></dict></plist>' > \"build-host/{0}/Contents/Info.plist\"\necho {1} > \"build-host/{0}/Contents/MacOS/Pangea\"\n",
                b.built, b.name
            ));
        }
        script.push_str("echo '{\"ok\": true, \"stage\": \"done\", \"installed\": false, \"auval\": \"skipped\", \"check\": \"skipped\", \"seconds\": 1}'\n");
        std::fs::write(dir.join("build-host.sh"), script).unwrap();
    }

    /// Apple's check, made to pass or fail; never a scratch run.
    struct Fake {
        passes: bool,
        scratch: bool,
    }
    impl System for Fake {
        fn scratch(&self) -> bool {
            self.scratch
        }
        fn restart_registrar(&self) {}
        fn auval(&self, kind: &str, _code: &str) -> (bool, String) {
            // The instrument fails, as a broken Audio Unit would.
            let ok = self.passes || kind != "aumu";
            (ok, if ok { "AU VALIDATION SUCCEEDED".into() } else { "ERROR: Cannot open component".into() })
        }
    }

    #[tokio::test]
    async fn a_failed_apple_check_is_never_called_installed() {
        let t = TempDir::new("auval");
        let (shell_dir, plugins, support) = (t.0.join("shell"), t.0.join("Plug-Ins"), t.0.join("support"));
        building_shell(&shell_dir);

        let r = install_with(&shell_dir, &plugins, &support, &Quiet, &Quiet, &Fake { passes: false, scratch: false }).await;
        assert!(!r.ok);
        assert_eq!(r.stage, "auval");
        assert!(r.message.contains("Apple's check of Pangea Instrument did not pass"), "{}", r.message);
        assert!(!stamp_path(&support).exists(), "no stamp for a plugin Apple's check refused");
        let after = detect(&plugins, &shell_dir, &support);
        assert_eq!(after.state, "check-failed", "{after:?}");

        // Installed again, and passing: the stamp says so and the note goes.
        let r = install_with(&shell_dir, &plugins, &support, &Quiet, &Quiet, &Fake { passes: true, scratch: false }).await;
        assert!(r.ok, "{}", r.message);
        let stamp = read_stamp(&support).expect("a stamp once the check passed");
        assert_eq!(stamp.validation, "passed");
        assert!(!failed_path(&support).exists());
        assert_eq!(detect(&plugins, &shell_dir, &support).state, "installed");

        // A scratch run leaves the check out and says so in the stamp.
        let r = install_with(&shell_dir, &plugins, &support, &Quiet, &Quiet, &Fake { passes: false, scratch: true }).await;
        assert!(r.ok);
        assert_eq!(read_stamp(&support).unwrap().validation, "skipped in a scratch run");
        assert!(r.auval.iter().all(|(_, v)| v == "skipped"));
    }
}
