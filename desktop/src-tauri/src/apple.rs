//! Apple's build tools: the compiler, the linker and the macOS SDK every Pangea build uses.
//!
//! "Installed" is not the same as "works", so the check is a small C++ program that must
//! compile, link and run, with the clean environment every build gets. When the default does not
//! work the app tries what it can mend without a password or Terminal, in this order:
//!
//! - the developer folder `xcode-select` chose, then the Command Line Tools at
//!   `/Library/Developer/CommandLineTools` when they are there too (with `DEVELOPER_DIR`), which
//!   gets round an Xcode whose licence nobody has accepted and a selection that points at an
//!   Xcode since deleted;
//! - in each, the default SDK, then every `MacOSX*.sdk` it holds, newest first (with `SDKROOT`):
//!   a stray SDK from a beta of the Command Line Tools, newer than the linker, makes the default
//!   fail to link even an empty program (`docs/setup-a-mac.md`), and the next SDK down works.
//!
//! The first combination that works is used for every build the app starts (`prereqs::command`).
//! When none does, the state says why in plain words. When the tools are missing, the app starts
//! Apple's own installer (`xcode-select --install`, run as a process of the app, never typed by
//! the member) and watches for it to finish.

use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use serde::Serialize;

use crate::{paths, prereqs, AppState};

pub const CLT_DIR: &str = "/Library/Developer/CommandLineTools";
/// How long Apple's installer usually takes, for the page.
pub const INSTALL_ESTIMATE: &str = "about 5 to 30 minutes, depending on your connection";
/// After the installer was started, how long the app waits for its window before it calls the
/// installer stopped (the dialog can take a few seconds to appear).
const INSTALLER_GRACE: Duration = Duration::from_secs(30);

pub const MISSING_MESSAGE: &str = "Pangea makes plugins on your Mac, so it needs Apple's build tools. macOS will ask you to install them. It takes about 5 to 30 minutes, and you can sign in while it runs.";
pub const INSTALLING_MESSAGE: &str = "Apple's installer is open. Choose Install, then Agree. It downloads about 1 GB and usually takes 5 to 30 minutes. This turns to a tick by itself when it has finished.";
pub const STOPPED_MESSAGE: &str = "Apple's installer stopped before the tools were installed. Press Install to try again. If it said the software is not currently available, Apple's server did not have the tools just then: try again in a few minutes, or get the Command Line Tools from Apple's download page.";
pub const LICENCE_MESSAGE: &str = "Xcode is installed, but its licence has not been accepted yet, so nothing can be built. Open Xcode once and choose Agree, then press Check again.";
pub const BROKEN_MESSAGE: &str = "Apple's build tools are installed but could not make a small test program. Press Install to have Apple's installer put them right, or Copy details for Pangea's help.";
pub const APPLE_DOWNLOADS: &str = "https://developer.apple.com/download/all/?q=Command%20Line%20Tools";

/// What the check found, for the page and for every build.
#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct AppleTools {
    /// `ready`, `missing`, `installing`, `stopped` (the installer the app started ended without
    /// the tools), `licence` (Xcode's licence not accepted) or `broken`.
    pub state: String,
    /// One plain sentence for the member.
    pub message: String,
    /// The developer folder builds use (`xcode-select -p`, or the Command Line Tools).
    pub developer_dir: Option<String>,
    /// Set when builds need `DEVELOPER_DIR` because `xcode-select` points elsewhere.
    pub developer_dir_override: Option<String>,
    /// Set when builds need `SDKROOT` because the default SDK does not link.
    pub sdk: Option<String>,
    /// The compiler's first line, e.g. "Apple clang version 21.0.0 (clang-2100.4.1.1)".
    pub compiler: Option<String>,
    /// What the failed attempts said, for Copy details.
    pub details: String,
    /// How long Apple's installer usually takes, while it is still to run or running.
    pub estimate: Option<String>,
    /// Apple's own download page for the tools, when the installer could not get them.
    pub help_url: Option<String>,
}

impl AppleTools {
    fn new(state: &str, message: &str) -> AppleTools {
        let to_install = matches!(state, "missing" | "installing" | "stopped");
        AppleTools {
            state: state.into(),
            message: message.into(),
            developer_dir: None,
            developer_dir_override: None,
            sdk: None,
            compiler: None,
            details: String::new(),
            estimate: to_install.then(|| INSTALL_ESTIMATE.to_string()),
            help_url: matches!(state, "stopped" | "broken").then(|| APPLE_DOWNLOADS.to_string()),
        }
    }
    pub fn is_ready(&self) -> bool {
        self.state == "ready"
    }
}

/// What `xcode-select --install` answered.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum InstallStart {
    /// Apple's installer was asked to show its window.
    Requested,
    /// The Command Line Tools are already there (a check again tells whether they work).
    AlreadyInstalled,
    /// Anything else, with what it said.
    Failed(String),
}

pub fn parse_install_start(success: bool, output: &str) -> InstallStart {
    let text = output.to_lowercase();
    if text.contains("install requested") || text.contains("requesting install") {
        InstallStart::Requested
    } else if text.contains("already installed") {
        InstallStart::AlreadyInstalled
    } else if success {
        InstallStart::Requested
    } else {
        InstallStart::Failed(output.trim().to_string())
    }
}

/// Why a test program did not build.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LinkFailure {
    /// Xcode's licence has not been accepted.
    Licence,
    /// The tools are not there at all (the shim asked for them).
    NoTools,
    /// Anything else: a bad SDK, a broken install.
    Other,
}

pub fn classify_failure(output: &str) -> LinkFailure {
    let text = output.to_lowercase();
    if text.contains("xcode license") || text.contains("xcodebuild -license") || text.contains("license agreement") {
        LinkFailure::Licence
    } else if text.contains("no developer tools were found")
        || text.contains("requires the command line developer tools")
        || text.contains("invalid active developer path")
    {
        LinkFailure::NoTools
    } else {
        LinkFailure::Other
    }
}

/// Parses "MacOSX26.5.sdk" into (26, 5, 0) for ordering; `None` for "MacOSX.sdk" and others.
fn sdk_version(name: &str) -> Option<(u32, u32, u32)> {
    let v = name.strip_prefix("MacOSX")?.strip_suffix(".sdk")?;
    let mut parts = v.split('.').map(|p| p.parse::<u32>().ok());
    let major = parts.next()??;
    let minor = parts.next().unwrap_or(Some(0))?;
    let patch = parts.next().unwrap_or(Some(0))?;
    Some((major, minor, patch))
}

/// The SDK folders where a developer folder keeps them: the Command Line Tools' `SDKs/`, or
/// Xcode's `Platforms/MacOSX.platform/Developer/SDKs/`.
pub fn sdk_dirs(developer_dir: &Path) -> Vec<PathBuf> {
    vec![
        developer_dir.join("SDKs"),
        developer_dir.join("Platforms/MacOSX.platform/Developer/SDKs"),
    ]
}

/// The SDKs to try, newest first. Links (`MacOSX.sdk`, `MacOSX27.sdk`) are left out: each
/// points at one of the versioned folders, which are tried themselves.
pub fn sdk_candidates(entries: &[(PathBuf, bool)]) -> Vec<PathBuf> {
    let mut found: Vec<((u32, u32, u32), PathBuf)> = entries
        .iter()
        .filter(|(_, is_link)| !is_link)
        .filter_map(|(p, _)| {
            let name = p.file_name()?.to_str()?;
            Some((sdk_version(name)?, p.clone()))
        })
        .collect();
    found.sort_by(|a, b| b.0.cmp(&a.0).then_with(|| a.1.cmp(&b.1)));
    found.dedup_by(|a, b| a.1 == b.1);
    found.into_iter().map(|(_, p)| p).collect()
}

fn list_sdks(developer_dir: &Path) -> Vec<PathBuf> {
    let mut entries = Vec::new();
    for dir in sdk_dirs(developer_dir) {
        let Ok(read) = std::fs::read_dir(&dir) else { continue };
        for e in read.flatten() {
            let is_link = e.file_type().map(|t| t.is_symlink()).unwrap_or(false);
            entries.push((e.path(), is_link));
        }
    }
    sdk_candidates(&entries)
}

/// True when a developer folder holds a compiler (Command Line Tools or Xcode layout), so the
/// test program can be built without the shim asking to install anything.
pub fn has_compiler(developer_dir: &Path) -> bool {
    developer_dir.join("usr/bin/clang").is_file()
        || developer_dir.join("Toolchains/XcodeDefault.xctoolchain/usr/bin/clang").is_file()
}

/// One attempt at the test program.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Attempt {
    pub developer_dir_override: Option<String>,
    pub sdk: Option<String>,
}

/// The order of attempts: for each developer folder (the selected one first), the default SDK,
/// then each SDK it holds. `selected` is what `xcode-select -p` printed, if it exists.
pub fn plan_attempts(selected: Option<&Path>, clt_present: bool, sdks_of: &dyn Fn(&Path) -> Vec<PathBuf>) -> Vec<Attempt> {
    let mut dirs: Vec<(PathBuf, bool)> = Vec::new();
    if let Some(s) = selected {
        dirs.push((s.to_path_buf(), false));
    }
    let clt = PathBuf::from(CLT_DIR);
    if clt_present && selected != Some(clt.as_path()) {
        dirs.push((clt, true));
    }
    let mut attempts = Vec::new();
    for (dir, is_override) in dirs {
        let dev = is_override.then(|| dir.display().to_string());
        attempts.push(Attempt { developer_dir_override: dev.clone(), sdk: None });
        for sdk in sdks_of(&dir) {
            attempts.push(Attempt { developer_dir_override: dev.clone(), sdk: Some(sdk.display().to_string()) });
        }
    }
    attempts
}

const TEST_PROGRAM: &str = "#include <vector>\n#include <cstdio>\nint main() { std::vector<int> v{1, 2}; std::printf(\"%zu\\n\", v.size()); return v.size() == 2 ? 0 : 1; }\n";

/// Builds and runs the test program with one attempt's settings. `Ok(compiler line)` or
/// `Err(what it said)`.
async fn try_build(work: &Path, attempt: &Attempt) -> Result<String, String> {
    let src = work.join("check.cpp");
    let exe = work.join("check");
    let _ = std::fs::remove_file(&exe);
    if let Err(e) = std::fs::write(&src, TEST_PROGRAM) {
        return Err(format!("could not write {}: {e}", src.display()));
    }
    let mut cmd = prereqs::command_with_apple("/usr/bin/c++", attempt.developer_dir_override.as_deref(), attempt.sdk.as_deref());
    cmd.arg("-std=c++17").arg(&src).arg("-o").arg(&exe).current_dir(work);
    let out = match tokio::time::timeout(Duration::from_secs(120), cmd.output()).await {
        Ok(Ok(o)) => o,
        Ok(Err(e)) => return Err(format!("could not run the compiler: {e}")),
        Err(_) => return Err("the compiler did not finish within two minutes".into()),
    };
    let said = String::from_utf8_lossy(&out.stdout).to_string() + &String::from_utf8_lossy(&out.stderr);
    if !out.status.success() {
        return Err(said.trim().to_string());
    }
    match prereqs::command(&exe.display().to_string()).output().await {
        Ok(o) if o.status.success() && String::from_utf8_lossy(&o.stdout).trim() == "2" => {}
        Ok(o) => return Err(format!("the test program built but did not run: exit {:?}", o.status.code())),
        Err(e) => return Err(format!("the test program built but could not be started: {e}")),
    }
    let mut version = prereqs::command_with_apple("/usr/bin/c++", attempt.developer_dir_override.as_deref(), attempt.sdk.as_deref());
    version.arg("--version");
    let line = match version.output().await {
        Ok(o) => String::from_utf8_lossy(&o.stdout).lines().next().unwrap_or("").trim().to_string(),
        Err(_) => String::new(),
    };
    Ok(line)
}

/// `xcode-select -p`, when it names a folder that exists.
async fn selected_developer_dir() -> Option<PathBuf> {
    let out = prereqs::command("/usr/bin/xcode-select").arg("-p").output().await.ok()?;
    if !out.status.success() {
        return None;
    }
    let path = PathBuf::from(String::from_utf8_lossy(&out.stdout).trim());
    path.is_dir().then_some(path)
}

/// Whether Apple's installer window is open.
pub async fn installer_running() -> bool {
    prereqs::command("/usr/bin/pgrep")
        .args(["-f", "Install Command Line Developer Tools"])
        .output()
        .await
        .map(|o| o.status.success())
        .unwrap_or(false)
}

/// When the app started Apple's installer, and whether its window has been seen since.
#[derive(Debug, Default, Clone, Copy)]
pub struct InstallWatch {
    pub started_at: Option<Instant>,
    pub seen_running: bool,
}

/// The state while the tools are missing: installing while the installer is open (or the app
/// started it a moment ago and its window has not shown yet), stopped when it ended without
/// them, missing when it was never started.
pub fn missing_state(watch: &InstallWatch, running: bool, now: Instant) -> AppleTools {
    if running {
        return AppleTools::new("installing", INSTALLING_MESSAGE);
    }
    match watch.started_at {
        Some(at) if !watch.seen_running && now.duration_since(at) < INSTALLER_GRACE => {
            AppleTools::new("installing", INSTALLING_MESSAGE)
        }
        Some(_) => AppleTools::new("stopped", STOPPED_MESSAGE),
        None => AppleTools::new("missing", MISSING_MESSAGE),
    }
}

/// The whole check. `work` is a folder the test program may be written to.
pub async fn detect(work: &Path, watch: &InstallWatch) -> AppleTools {
    if let Err(e) = std::fs::create_dir_all(work) {
        let mut t = AppleTools::new("broken", BROKEN_MESSAGE);
        t.details = format!("could not create {}: {e}", work.display());
        return t;
    }
    let selected = selected_developer_dir().await.filter(|d| has_compiler(d));
    let clt_present = has_compiler(Path::new(CLT_DIR));
    if selected.is_none() && !clt_present {
        return missing_state(watch, installer_running().await, Instant::now());
    }
    let attempts = plan_attempts(selected.as_deref(), clt_present, &list_sdks);
    let mut details = Vec::new();
    let mut licence = false;
    for attempt in &attempts {
        match try_build(work, attempt).await {
            Ok(compiler) => {
                let mut t = AppleTools::new("ready", "Apple's build tools are ready.");
                t.developer_dir = attempt
                    .developer_dir_override
                    .clone()
                    .or_else(|| selected.as_ref().map(|d| d.display().to_string()));
                t.developer_dir_override = attempt.developer_dir_override.clone();
                t.sdk = attempt.sdk.clone();
                t.compiler = Some(compiler).filter(|c| !c.is_empty());
                t.details = details.join("\n\n");
                return t;
            }
            Err(said) => {
                if classify_failure(&said) == LinkFailure::Licence {
                    licence = true;
                }
                details.push(format!(
                    "DEVELOPER_DIR={} SDKROOT={}:\n{}",
                    attempt.developer_dir_override.as_deref().unwrap_or("(selected)"),
                    attempt.sdk.as_deref().unwrap_or("(default)"),
                    said.lines().take(12).collect::<Vec<_>>().join("\n")
                ));
            }
        }
    }
    let mut t = if licence { AppleTools::new("licence", LICENCE_MESSAGE) } else { AppleTools::new("broken", BROKEN_MESSAGE) };
    t.developer_dir = selected.map(|d| d.display().to_string());
    t.details = details.join("\n\n");
    t
}

/// Checks again, remembers the answer, and hands the developer folder and SDK it found to every
/// build. While Apple's installer runs, notes that its window was seen, so its end can be told.
pub async fn refresh(state: &AppState) -> AppleTools {
    let before = *state.apple_watch.lock().unwrap();
    if before.started_at.is_some() && !before.seen_running && installer_running().await {
        state.apple_watch.lock().unwrap().seen_running = true;
    }
    let watch = *state.apple_watch.lock().unwrap();
    let work = paths::support_dir()
        .map(|d| d.join("checks").join("apple"))
        .unwrap_or_else(|_| std::env::temp_dir().join("pangea-apple-check"));
    let found = detect(&work, &watch).await;
    if found.is_ready() {
        prereqs::set_apple(found.developer_dir_override.clone(), found.sdk.clone());
        *state.apple_watch.lock().unwrap() = InstallWatch::default();
    }
    *state.apple.lock().unwrap() = Some(found.clone());
    found
}

/// The last answer, or a fresh check when there is none yet.
pub async fn current(state: &AppState) -> AppleTools {
    let known = state.apple.lock().unwrap().clone();
    match known {
        Some(t) => t,
        None => refresh(state).await,
    }
}

/// The Install button: starts Apple's installer and says what is happening.
pub async fn install(state: &AppState) -> AppleTools {
    match start_install().await {
        InstallStart::Requested => {
            *state.apple_watch.lock().unwrap() = InstallWatch { started_at: Some(Instant::now()), seen_running: false };
            let t = AppleTools::new("installing", INSTALLING_MESSAGE);
            *state.apple.lock().unwrap() = Some(t.clone());
            t
        }
        InstallStart::AlreadyInstalled => refresh(state).await,
        InstallStart::Failed(said) => {
            let mut t = AppleTools::new("stopped", STOPPED_MESSAGE);
            t.details = said;
            *state.apple.lock().unwrap() = Some(t.clone());
            t
        }
    }
}

/// Runs Apple's installer (`xcode-select --install`) as a process of the app.
pub async fn start_install() -> InstallStart {
    match prereqs::command("/usr/bin/xcode-select").arg("--install").output().await {
        Ok(o) => {
            let said = String::from_utf8_lossy(&o.stdout).to_string() + &String::from_utf8_lossy(&o.stderr);
            parse_install_start(o.status.success(), &said)
        }
        Err(e) => InstallStart::Failed(format!("could not run xcode-select: {e}")),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn what_xcode_select_install_says_is_read() {
        assert_eq!(
            parse_install_start(true, "xcode-select: note: install requested for command line developer tools\n"),
            InstallStart::Requested
        );
        assert_eq!(
            parse_install_start(false, "xcode-select: note: No developer tools were found, requesting install.\n"),
            InstallStart::Requested
        );
        assert_eq!(
            parse_install_start(
                false,
                "xcode-select: error: command line tools are already installed, use \"Software Update\" in System Settings or the softwareupdate command line interface to install updates\n"
            ),
            InstallStart::AlreadyInstalled
        );
        assert_eq!(parse_install_start(false, "something else\n"), InstallStart::Failed("something else".into()));
    }

    #[test]
    fn an_unaccepted_xcode_licence_is_told_apart() {
        let licence = "You have not agreed to the Xcode license agreements. You must agree to both license agreements below in order to use Xcode.\n\nPlease run 'sudo xcodebuild -license' from within a Terminal window to review and agree to the Xcode and Apple SDKs license.";
        assert_eq!(classify_failure(licence), LinkFailure::Licence);
        let stray_sdk = "ld: tapi error: malformed file\nld: unknown architecture arm64e.x1\nclang: error: linker command failed with exit code 1";
        assert_eq!(classify_failure(stray_sdk), LinkFailure::Other);
        assert_eq!(
            classify_failure("xcrun: error: invalid active developer path (/Library/Developer/CommandLineTools), missing xcrun"),
            LinkFailure::NoTools
        );
    }

    #[test]
    fn sdks_are_tried_newest_first_without_the_links() {
        let base = PathBuf::from("/Library/Developer/CommandLineTools/SDKs");
        let entries = vec![
            (base.join("MacOSX.sdk"), true),
            (base.join("MacOSX12.1.sdk"), false),
            (base.join("MacOSX26.5.sdk"), false),
            (base.join("MacOSX26.sdk"), true),
            (base.join("MacOSX27.0.sdk"), false),
            (base.join("MacOSX27.sdk"), true),
            (base.join("README.txt"), false),
        ];
        assert_eq!(
            sdk_candidates(&entries),
            vec![base.join("MacOSX27.0.sdk"), base.join("MacOSX26.5.sdk"), base.join("MacOSX12.1.sdk")]
        );
        assert_eq!(sdk_version("MacOSX26.5.sdk"), Some((26, 5, 0)));
        assert_eq!(sdk_version("MacOSX.sdk"), None);
    }

    /// The stray beta SDK of `docs/setup-a-mac.md`: the default and 27.0 fail, 26.5 is next.
    #[test]
    fn the_attempts_go_from_the_selected_folder_to_the_command_line_tools() {
        let xcode = PathBuf::from("/Applications/Xcode.app/Contents/Developer");
        let sdks = |d: &Path| -> Vec<PathBuf> {
            if d == Path::new(CLT_DIR) {
                vec![PathBuf::from("/clt/MacOSX27.0.sdk"), PathBuf::from("/clt/MacOSX26.5.sdk")]
            } else {
                vec![PathBuf::from("/xcode/MacOSX27.2.sdk")]
            }
        };
        let plan = plan_attempts(Some(&xcode), true, &sdks);
        let shown: Vec<(Option<&str>, Option<&str>)> =
            plan.iter().map(|a| (a.developer_dir_override.as_deref(), a.sdk.as_deref())).collect();
        assert_eq!(
            shown,
            vec![
                (None, None),
                (None, Some("/xcode/MacOSX27.2.sdk")),
                (Some(CLT_DIR), None),
                (Some(CLT_DIR), Some("/clt/MacOSX27.0.sdk")),
                (Some(CLT_DIR), Some("/clt/MacOSX26.5.sdk")),
            ]
        );
        // The Command Line Tools selected themselves: no override, no second folder.
        let only = plan_attempts(Some(Path::new(CLT_DIR)), true, &sdks);
        assert_eq!(only.len(), 3);
        assert!(only.iter().all(|a| a.developer_dir_override.is_none()));
        // Nothing selected but the tools are there (a selection pointing at a deleted Xcode).
        let orphan = plan_attempts(None, true, &sdks);
        assert_eq!(orphan[0].developer_dir_override.as_deref(), Some(CLT_DIR));
    }

    #[test]
    fn the_installer_is_watched_until_it_ends() {
        let now = Instant::now();
        assert_eq!(missing_state(&InstallWatch::default(), false, now).state, "missing");
        assert_eq!(missing_state(&InstallWatch::default(), true, now).state, "installing");
        let just_started = InstallWatch { started_at: Some(now), seen_running: false };
        assert_eq!(missing_state(&just_started, false, now + Duration::from_secs(5)).state, "installing");
        assert_eq!(missing_state(&just_started, false, now + Duration::from_secs(40)).state, "stopped");
        let seen = InstallWatch { started_at: Some(now), seen_running: true };
        assert_eq!(missing_state(&seen, true, now + Duration::from_secs(600)).state, "installing");
        assert_eq!(missing_state(&seen, false, now + Duration::from_secs(5)).state, "stopped");
    }

    #[test]
    fn the_messages_follow_the_copy_rules() {
        for text in [MISSING_MESSAGE, INSTALLING_MESSAGE, STOPPED_MESSAGE, LICENCE_MESSAGE, BROKEN_MESSAGE, INSTALL_ESTIMATE] {
            assert!(!text.contains('\u{2014}') && !text.contains('\u{2013}') && !text.contains('!'), "copy rules: {text}");
            assert!(!text.contains("xcode-select") && !text.contains("sudo"), "no commands for the member: {text}");
        }
    }

    /// On this Mac, for real: the tools are found and a program links (skipped where there are none).
    #[tokio::test]
    async fn the_tools_on_this_mac_build_a_program() {
        if !has_compiler(Path::new(CLT_DIR)) && !Path::new("/Applications/Xcode.app").is_dir() {
            return;
        }
        let work = std::env::temp_dir().join(format!("pangea-apple-test-{}", std::process::id()));
        let found = detect(&work, &InstallWatch::default()).await;
        let _ = std::fs::remove_dir_all(&work);
        assert_eq!(found.state, "ready", "{found:?}");
        assert!(found.compiler.as_deref().unwrap_or("").contains("clang"), "{found:?}");
    }
}
