//! The self-check at the end of Setup: Pangea builds a small test plugin and the Pangea plugin
//! opens it, with the repository's own check tools.
//!
//! 1. **A test plugin**: `build-module.sh` builds the shell's example Tape Drift as a module, as
//!    for any plugin the member opens, into the app's own folder (`selfcheck/`, never the
//!    member's modules): the app's Faust, Apple's compiler and linker with the SDK the app chose,
//!    the shell. Its module number must be the shell's.
//! 2. **The Pangea plugin opens it**: `build-host.sh --check` builds `pangea-host-check` and runs
//!    `tools/test-host.sh`, the Pangea plugin's own checks: twelve modules built from the shell's
//!    examples and fixtures (effects, instruments with their effect sections, logic modules, MIDI
//!    tools, one of an old module number) loaded into the plugin's own processor for each kind,
//!    played, swapped while playing, the built VST3s loaded as a DAW loads them, and the web
//!    panel drawn in a window for a few seconds.
//! 3. **The installed plugin is the one checked**: each installed bundle's executable must be the
//!    same bytes as the one just checked.
//!
//! What it cannot see is inside the member's DAW: whether Logic, Live or Bitwig has scanned the
//! plugin. The host service notes the first request the Pangea plugin makes from a DAW
//! (`AppState::plugin_seen`), and Setup ticks "Pangea is working in your DAW" then.

use std::ffi::OsString;
use std::path::{Path, PathBuf};
use std::time::Instant;

use anyhow::{bail, Result};
use serde::Serialize;
use serde_json::Value;

use crate::build::{self, JobLog};
use crate::host_install::{self, Stages, BUNDLES};
use crate::tools::sha256_file;

pub const LOG_EVENT: &str = "self-check-log";
pub const STAGE_EVENT: &str = "self-check-stage";

pub const PROVES: &str = "Pangea's own Faust, Apple's compiler and Pangea's shell make a working plugin on this Mac, and the Pangea plugin you installed opens Pangea plugins of every kind, plays them, changes to a new version while it plays, and shows its panel.";
pub const CANNOT: &str = "It cannot look inside your DAW. Add Pangea to a track: this turns to a tick when the Pangea plugin asks the app for your plugins.";

#[derive(Debug, Clone, Serialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct CheckReport {
    pub ok: bool,
    /// `module`, `host`, `installed` or `done`: where it ended.
    pub stage: String,
    pub message: String,
    pub seconds: f64,
}

/// The executable inside a bundle (`Contents/MacOS/<the only file>`).
fn executable(bundle: &Path) -> Option<PathBuf> {
    let dir = bundle.join("Contents/MacOS");
    std::fs::read_dir(&dir).ok()?.flatten().map(|e| e.path()).find(|p| p.is_file())
}

/// True when every installed bundle's executable is byte for byte the one built in `built`.
pub fn installed_is_built(plugins: &Path, built: &Path) -> Result<bool> {
    for b in &BUNDLES {
        let (Some(have), Some(made)) = (executable(&host_install::installed_path(plugins, b)), executable(&built.join(b.built))) else {
            return Ok(false);
        };
        if sha256_file(&have)? != sha256_file(&made)? {
            return Ok(false);
        }
    }
    Ok(true)
}

pub async fn run(shell_dir: &Path, plugins: &Path, support: &Path, log: &dyn JobLog, stages: &dyn Stages) -> CheckReport {
    let started = Instant::now();
    let mut report = CheckReport { ok: false, stage: "module".into(), message: String::new(), seconds: 0.0 };
    if let Err(e) = steps(shell_dir, plugins, support, log, stages, &mut report).await {
        log.app(&format!("error: {e:#}"));
        if report.message.is_empty() {
            report.message = "The check could not finish. Press Try again; if it stops again, press Copy details for Pangea's help.".into();
        }
    }
    report.seconds = started.elapsed().as_secs_f64();
    report
}

async fn steps(shell_dir: &Path, plugins: &Path, support: &Path, log: &dyn JobLog, stages: &dyn Stages, report: &mut CheckReport) -> Result<()> {
    // 1. A test plugin, as a module
    stages.stage("module");
    let example = shell_dir.join("examples/tape-drift");
    let module = support.join("selfcheck/modules/selfcheck/v1");
    let _ = std::fs::remove_dir_all(&module);
    let script = shell_dir.join("build-module.sh");
    let args: Vec<OsString> = vec![
        example.clone().into_os_string(),
        module.clone().into_os_string(),
        "--project-id".into(),
        "selfcheck".into(),
        "--version-number".into(),
        "1".into(),
    ];
    log.app(&format!("$ bash {} {} {}", script.display(), example.display(), module.display()));
    let (ok, summary) = build::run_script(&script, &args, shell_dir, log).await?;
    let built = ok && summary.as_ref().and_then(|s| s.get("ok")).and_then(Value::as_bool).unwrap_or(false);
    if !built {
        let why = summary.as_ref().and_then(|s| s.get("error")).and_then(Value::as_str).unwrap_or("it stopped without saying why").to_string();
        report.message = "Pangea could not build its test plugin. Press Try again; if it stops again, press Copy details for Pangea's help.".into();
        bail!("build-module.sh: {why}");
    }
    let abi = summary.as_ref().and_then(|s| s.get("abi")).and_then(Value::as_u64);
    if abi.map(|a| a as u32) != host_install::shell_abi(shell_dir) {
        report.message = "Pangea's test plugin has another module number than its shell. Get Pangea's tools again.".into();
        bail!("module abi {abi:?}, shell {:?}", host_install::shell_abi(shell_dir));
    }
    log.app(&format!("the test plugin is built: {}", module.join("module.dylib").display()));

    // 2. The Pangea plugin's own checks
    report.stage = "host".into();
    stages.stage("host");
    let script = shell_dir.join("build-host.sh");
    log.app(&format!("$ bash {} --check", script.display()));
    let (ok, summary) = build::run_script(&script, &["--check".into()], shell_dir, log).await?;
    let checked = ok
        && summary.as_ref().and_then(|s| s.get("ok")).and_then(Value::as_bool).unwrap_or(false)
        && summary.as_ref().and_then(|s| s.get("check")).and_then(Value::as_str) == Some("passed");
    if !checked {
        let stage = summary.as_ref().and_then(|s| s.get("stage")).and_then(Value::as_str).unwrap_or("check");
        report.message = if stage == "check" {
            "The Pangea plugin did not pass its checks with the test plugins. Press Copy details for Pangea's help.".into()
        } else {
            "The Pangea plugin's checks could not be built. Press Copy details for Pangea's help.".into()
        };
        bail!("build-host.sh --check stopped at {stage}");
    }

    // 3. The installed plugin is the one checked
    report.stage = "installed".into();
    stages.stage("installed");
    if !installed_is_built(plugins, &shell_dir.join("build-host"))? {
        report.message = "The Pangea plugin in your plug-in folders is not the one Pangea just checked. Install it again, then check again.".into();
        bail!("the installed bundles differ from the built ones");
    }
    report.ok = true;
    report.stage = "done".into();
    report.message = PROVES.into();
    stages.stage("done");
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn what_the_check_says_follows_the_copy_rules() {
        for text in [PROVES, CANNOT] {
            assert!(!text.contains('\u{2014}') && !text.contains('\u{2013}') && !text.contains('!'), "copy rules: {text}");
        }
    }

    #[test]
    fn the_installed_plugin_must_be_the_one_built() {
        let dir = std::env::temp_dir().join(format!("pangea-selfcheck-test-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        let (plugins, built) = (dir.join("Plug-Ins"), dir.join("build-host"));
        for b in &BUNDLES {
            for (root, rel) in [(&built, b.built.to_string()), (&plugins, host_install::installed_path(Path::new(""), b).display().to_string())] {
                let exe = root.join(rel.trim_start_matches('/')).join("Contents/MacOS");
                std::fs::create_dir_all(&exe).unwrap();
                std::fs::write(exe.join("Pangea"), b.name).unwrap();
            }
        }
        assert!(installed_is_built(&plugins, &built).unwrap());
        let first = host_install::installed_path(&plugins, &BUNDLES[0]).join("Contents/MacOS/Pangea");
        std::fs::write(first, "an older build").unwrap();
        assert!(!installed_is_built(&plugins, &built).unwrap());
        std::fs::remove_dir_all(&dir).unwrap();
    }
}
