//! Where the app keeps what it writes on the member's Mac, in one place.
//!
//! Everything hangs off one home folder: `$HOME`, or `PANGEA_DESKTOP_HOME` when it is set.
//! The override is for tests and for running every setup step for real into a scratch folder
//! (`docs/desktop-setup.md`): the app's own folder, the plug-in folders the Pangea plugin
//! is installed into, and the `HOME` every build script sees all move with it, and the steps
//! that would reach the real Mac (restarting the Audio Unit registrar, `auval`, which asks that
//! registrar) say so and are left out.
//!
//! `PANGEA_DESKTOP_SYSTEM_PATH` likewise replaces the folders searched after Apple's own for
//! tools the app does not carry (Homebrew's by default), so a run can imitate a Mac that has
//! only Apple's tools by setting it to nothing. `PANGEA_DESKTOP_PAYLOAD` (toolchain.rs) points
//! at a staged payload in place of the bundle's.
//!
//! **The three are honoured only in a development or test build** (`debug_assertions`). A
//! release build, which is what members get, ignores them: otherwise anything that can set an
//! environment variable for the member's session could make the app run another `faust`,
//! install the Pangea plugin elsewhere and leave Apple's check out.

use std::ffi::OsString;
use std::path::PathBuf;

use anyhow::{Context, Result};

pub const HOME_OVERRIDE: &str = "PANGEA_DESKTOP_HOME";
pub const SYSTEM_PATH_OVERRIDE: &str = "PANGEA_DESKTOP_SYSTEM_PATH";
/// Homebrew on Apple silicon, then the older shared place.
pub const DEFAULT_SYSTEM_PATH: &str = "/opt/homebrew/bin:/opt/homebrew/sbin:/usr/local/bin";

/// Whether this build honours the overrides: a development or test build does, a release does not.
pub const OVERRIDES_HONOURED: bool = cfg!(debug_assertions);

/// An override's value, when this build honours overrides and it is set and not empty.
pub fn override_var(name: &str) -> Option<OsString> {
    honoured(OVERRIDES_HONOURED, std::env::var_os(name))
}

/// `value` when `allowed`, and not empty. Separate so tests can ask what a release build does.
pub fn honoured(allowed: bool, value: Option<OsString>) -> Option<OsString> {
    value.filter(|v| allowed && !v.is_empty())
}

/// The home folder: `PANGEA_DESKTOP_HOME` when honoured and set, else `$HOME`.
pub fn home() -> Result<PathBuf> {
    if let Some(h) = override_var(HOME_OVERRIDE) {
        return Ok(PathBuf::from(h));
    }
    std::env::var_os("HOME").map(PathBuf::from).context("HOME is not set")
}

/// True when `PANGEA_DESKTOP_HOME` moved the app away from the real home folder: the steps that
/// would reach the real Mac (the Audio Unit registrar, `auval`) are then left out. Always false
/// in a release build.
pub fn is_scratch_home() -> bool {
    override_var(HOME_OVERRIDE).is_some()
}

/// `<home>/Library/Audio/Plug-Ins`, which holds `Components` (Audio Units) and `VST3`.
pub fn plugins_dir() -> Result<PathBuf> {
    Ok(home()?.join("Library").join("Audio").join("Plug-Ins"))
}

/// `<home>/Library/Application Support/Pangea`: projects, the Pangea plugin's folder, the
/// app's own tools and its copy of the shell.
pub fn support_dir() -> Result<PathBuf> {
    Ok(home()?.join("Library").join("Application Support").join("Pangea"))
}

/// The folders searched after Apple's own (`PANGEA_DESKTOP_SYSTEM_PATH` when honoured, else
/// Homebrew's).
pub fn system_path() -> String {
    system_path_from(OVERRIDES_HONOURED, std::env::var_os(SYSTEM_PATH_OVERRIDE))
}

/// `system_path`, with whether overrides count and the variable given. Set to nothing, the
/// override leaves Homebrew out, so it is read even when empty.
pub fn system_path_from(allowed: bool, value: Option<OsString>) -> String {
    match value {
        Some(p) if allowed => p.to_string_lossy().to_string(),
        _ => DEFAULT_SYSTEM_PATH.to_string(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn everything_hangs_off_one_home() {
        let home = home().unwrap();
        assert_eq!(support_dir().unwrap(), home.join("Library/Application Support/Pangea"));
        assert_eq!(crate::settings::projects_dir().unwrap(), home.join("Library/Application Support/Pangea/projects"));
    }

    /// What a release build, which members get, does with the three overrides: nothing.
    #[test]
    fn a_release_build_ignores_the_overrides() {
        let scratch = Some(OsString::from("/tmp/scratch-home"));
        assert_eq!(honoured(false, scratch.clone()), None);
        assert_eq!(honoured(true, scratch.clone()), scratch);
        assert_eq!(honoured(true, Some(OsString::new())), None);
        assert_eq!(system_path_from(false, Some(OsString::new())), DEFAULT_SYSTEM_PATH);
        assert_eq!(system_path_from(true, Some(OsString::new())), "");
        assert_eq!(OVERRIDES_HONOURED, cfg!(debug_assertions));
    }

    /// Run with `cargo test --release`: in a release build the variables are set and still ignored.
    #[cfg(not(debug_assertions))]
    #[test]
    fn in_this_release_build_the_overrides_are_off() {
        std::env::set_var(HOME_OVERRIDE, "/tmp/never-the-home");
        std::env::set_var(SYSTEM_PATH_OVERRIDE, "");
        std::env::set_var(crate::toolchain::PAYLOAD_OVERRIDE, "/tmp/never-a-payload");
        assert!(!is_scratch_home());
        assert_ne!(home().unwrap(), PathBuf::from("/tmp/never-the-home"));
        assert_eq!(system_path(), DEFAULT_SYSTEM_PATH);
        assert!(override_var(crate::toolchain::PAYLOAD_OVERRIDE).is_none());
    }
}
