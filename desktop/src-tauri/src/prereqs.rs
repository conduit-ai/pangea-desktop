//! Checks the tools the native build needs and says exactly how to fix what is missing.

use std::path::{Path, PathBuf};
use std::process::Stdio;
use std::sync::RwLock;

use serde::Serialize;
use tokio::process::Command;

use crate::apple::AppleTools;
use crate::paths;
use crate::settings::ShellInfo;

/// The pinned compiler versions, compiled in from `library/faust-versions.json` so a
/// packaged app knows what the web app was built against.
const FAUST_VERSIONS_JSON: &str = include_str!("../../../library/faust-versions.json");

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Check {
    pub id: String,
    pub name: String,
    /// `ok`, `warn` (usable, but worth fixing) or `missing`.
    pub status: String,
    pub detail: String,
    /// A command the user can paste into Terminal, or a sentence when it is not a command.
    pub fix: Option<String>,
}

impl Check {
    fn ok(id: &str, name: &str, detail: impl Into<String>) -> Self {
        Self { id: id.into(), name: name.into(), status: "ok".into(), detail: detail.into(), fix: None }
    }
    fn warn(id: &str, name: &str, detail: impl Into<String>, fix: impl Into<String>) -> Self {
        Self { id: id.into(), name: name.into(), status: "warn".into(), detail: detail.into(), fix: Some(fix.into()) }
    }
    fn missing(id: &str, name: &str, detail: impl Into<String>, fix: impl Into<String>) -> Self {
        Self { id: id.into(), name: name.into(), status: "missing".into(), detail: detail.into(), fix: Some(fix.into()) }
    }
}

/// What every child process gets on top of the fixed environment below: the folders of the tools
/// the app carries or fetched, first on the `PATH`, and Apple's developer folder and SDK when the
/// defaults do not build (`apple.rs`). Filled in as the app finds them; empty until then.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct BuildEnv {
    pub tool_dirs: Vec<PathBuf>,
    pub developer_dir: Option<String>,
    pub sdkroot: Option<String>,
}

static BUILD_ENV: RwLock<BuildEnv> = RwLock::new(BuildEnv { tool_dirs: Vec::new(), developer_dir: None, sdkroot: None });

pub fn build_env() -> BuildEnv {
    BUILD_ENV.read().map(|e| e.clone()).unwrap_or_default()
}

/// Apple's developer folder and SDK for every build, as `apple::detect` found them.
pub fn set_apple(developer_dir: Option<String>, sdkroot: Option<String>) {
    if let Ok(mut e) = BUILD_ENV.write() {
        e.developer_dir = developer_dir;
        e.sdkroot = sdkroot;
    }
}

/// The folders of the app's own tools, searched before anything else. The first is the app's
/// own `bin` (Faust, Ninja) when it carries one.
pub fn set_tool_dirs(dirs: Vec<PathBuf>) {
    if let Ok(mut e) = BUILD_ENV.write() {
        e.tool_dirs = dirs;
    }
}

/// The PATH every child process gets. Fixed, not inherited: a GUI app launched from
/// Finder has a minimal PATH, and one launched from a terminal inherits the user's
/// shell setup (pyenv, nvm, conda), which must not reach the build. The app's own tools come
/// first, then the system directories so `python3` is Apple's, then Homebrew's, where a
/// developer's CMake may be (`paths::system_path`).
pub fn tool_path_with(tool_dirs: &[PathBuf]) -> String {
    let mut parts: Vec<String> = tool_dirs.iter().map(|d| d.display().to_string()).collect();
    parts.push("/usr/bin:/bin:/usr/sbin:/sbin".to_string());
    let system = paths::system_path();
    if !system.is_empty() {
        parts.push(system);
    }
    parts.join(":")
}

pub fn tool_path() -> String {
    tool_path_with(&build_env().tool_dirs)
}

/// A command with a clean, explicit environment (see `tool_path`). `PYENV_*` and the like
/// are dropped on purpose: with them inherited, pyenv's Python hung inside `build.sh`
/// before it printed a line. `HOME` is `paths::home()`.
pub fn command(program: &str) -> Command {
    let env = build_env();
    command_with_apple(program, env.developer_dir.as_deref(), env.sdkroot.as_deref())
}

/// `command`, with this developer folder and SDK in place of the ones found (the check tries each).
pub fn command_with_apple(program: &str, developer_dir: Option<&str>, sdkroot: Option<&str>) -> Command {
    let mut cmd = Command::new(program);
    cmd.env_clear();
    cmd.env("PATH", tool_path());
    if let Ok(home) = paths::home() {
        cmd.env("HOME", home);
    }
    for key in ["USER", "LOGNAME", "TMPDIR"] {
        if let Ok(v) = std::env::var(key) {
            cmd.env(key, v);
        }
    }
    if let Some(d) = developer_dir {
        cmd.env("DEVELOPER_DIR", d);
    }
    if let Some(s) = sdkroot {
        cmd.env("SDKROOT", s);
    }
    cmd.env("LANG", "en_US.UTF-8");
    cmd.env("LC_ALL", "en_US.UTF-8");
    cmd.env("SHELL", "/bin/bash");
    cmd.env("PANGEA_DESKTOP", env!("CARGO_PKG_VERSION"));
    cmd.stdin(Stdio::null());
    cmd
}

/// The Faust version the native toolchain is pinned to (`native.faust`).
pub fn pinned_faust_version() -> Option<String> {
    let v: serde_json::Value = serde_json::from_str(FAUST_VERSIONS_JSON).ok()?;
    v.get("native")?.get("faust")?.as_str().map(str::to_string)
}

async fn run(cmd: &str, args: &[&str]) -> Option<(bool, String)> {
    let out = command(cmd).args(args).output().await.ok()?;
    let text = String::from_utf8_lossy(&out.stdout).to_string() + &String::from_utf8_lossy(&out.stderr);
    Some((out.status.success(), text.trim().to_string()))
}

fn first_line(s: &str) -> String {
    s.lines().next().unwrap_or("").trim().to_string()
}

/// How to mend a tool the app carries: in a release build (a member's) a sentence pointing at
/// Setup, never a command; in a development build, which carries nothing, Homebrew's command.
fn carried_fix(homebrew: &str) -> String {
    carried_fix_for(cfg!(debug_assertions), homebrew)
}

fn carried_fix_for(development: bool, homebrew: &str) -> String {
    if development {
        format!("A development build uses Homebrew's: {homebrew}")
    } else {
        "Pangea carries it. Open Setup: Pangea's tools says what is wrong.".to_string()
    }
}

/// The first `name` on the `PATH` builds get, as a build would find it.
pub fn find_tool(name: &str) -> Option<PathBuf> {
    tool_path().split(':').filter(|d| !d.is_empty()).map(|d| Path::new(d).join(name)).find(|p| p.is_file())
}

/// ", carried by the app" when a build finds the app's own copy of a tool, else ", at <path>".
fn where_from(name: &str) -> String {
    let Some(found) = find_tool(name) else { return String::new() };
    let fetched = paths::support_dir().map(|s| crate::tools::tools_dir(&s)).ok();
    let env = build_env();
    match env.tool_dirs.first() {
        _ if fetched.as_ref().is_some_and(|f| found.starts_with(f)) => ", fetched by the app".to_string(),
        Some(own) if found.starts_with(own) => ", carried by the app".to_string(),
        _ => format!(", at {}", found.display()),
    }
}

/// Parses "FAUST Version 2.88.0" (or "faust 2.88.0") into "2.88.0".
pub fn parse_faust_version(text: &str) -> Option<String> {
    text.split_whitespace()
        .find(|w| w.chars().next().is_some_and(|c| c.is_ascii_digit()) && w.contains('.'))
        .map(|w| w.trim_matches(|c: char| !c.is_ascii_digit() && c != '.').to_string())
}

pub async fn check_all(shell: &ShellInfo, apple: &AppleTools) -> Vec<Check> {
    let mut checks = Vec::new();
    let shell_source = shell.source.as_str();

    // Apple's build tools: there, and able to build a program (apple.rs)
    if apple.is_ready() {
        let mut detail = apple.compiler.clone().unwrap_or_else(|| "Ready".to_string());
        if let Some(sdk) = &apple.sdk {
            detail.push_str(&format!(", with the SDK {sdk}"));
        }
        if let Some(dev) = &apple.developer_dir_override {
            detail.push_str(&format!(", from {dev}"));
        }
        checks.push(Check::ok("xcode", "Apple's build tools", detail));
    } else {
        checks.push(Check::missing("xcode", "Apple's build tools", apple.message.clone(), "Use the button above this list."));
    }

    // cmake: the app's own once fetched (tools.rs), or one of 3.22 or later already on the Mac
    match run("cmake", &["--version"]).await {
        Some((true, out)) if crate::tools::parse_cmake_version(&out).is_some_and(|v| v >= crate::tools::CMAKE_MINIMUM) => {
            checks.push(Check::ok("cmake", "CMake", format!("{}{}", first_line(&out), where_from("cmake"))))
        }
        Some((true, out)) => checks.push(Check::missing(
            "cmake",
            "CMake",
            format!("{}{} is older than 3.22.", first_line(&out), where_from("cmake")),
            "Pangea fetches its own: press Get Pangea's tools.",
        )),
        _ => checks.push(Check::missing("cmake", "CMake", "Not here yet.", "Pangea fetches it: press Get Pangea's tools.")),
    }

    // ninja: the app's own in a packaged app (toolchain.rs)
    match run("ninja", &["--version"]).await {
        Some((true, out)) => checks.push(Check::ok("ninja", "Ninja", format!("ninja {}{}", first_line(&out), where_from("ninja")))),
        _ => checks.push(Check::missing("ninja", "Ninja", "Not found.", carried_fix("brew install ninja"))),
    }

    // faust, compared with the pinned version: the app's own in a packaged app
    let pinned = pinned_faust_version();
    match run("faust", &["--version"]).await {
        Some((true, out)) => {
            let found = parse_faust_version(&out).unwrap_or_else(|| first_line(&out));
            match &pinned {
                Some(p) if p != &found => checks.push(Check::warn(
                    "faust",
                    "Faust",
                    format!("Faust {found}{} found, Pangea expects {p}. Builds still work; the sound may differ slightly from the browser.", where_from("faust")),
                    carried_fix("brew upgrade faust"),
                )),
                _ => checks.push(Check::ok("faust", "Faust", format!("Faust {found}{}", where_from("faust")))),
            }
        }
        _ => checks.push(Check::missing(
            "faust",
            "Faust",
            match &pinned {
                Some(p) => format!("Not found. Pangea expects Faust {p}."),
                None => "Not found.".to_string(),
            },
            carried_fix("brew install faust"),
        )),
    }

    // the shell: the app's own copy, or a developer's folder named in Settings (settings::shell_info)
    let shell = shell.dir.trim();
    if shell.is_empty() {
        checks.push(Check::missing(
            "shell",
            "Pangea shell",
            "The packaged app carries the shell; this development build has none.",
            "Name the repository's shell folder in Settings, For developers.",
        ));
    } else {
        let script = Path::new(shell).join("build.sh");
        if script.is_file() {
            let whose = if shell_source == "app" { "the app's own" } else { "your own folder" };
            checks.push(Check::ok("shell", "Pangea shell", format!("{whose}, {}", Path::new(shell).display())));
        } else {
            checks.push(Check::missing(
                "shell",
                "Pangea shell",
                format!("{} has no build.sh.", shell),
                "Name the repository's shell folder in Settings, For developers, or clear it to use the app's own.",
            ));
        }

        // JUCE inside the shell: fetched and linked by the app into its own copy (tools.rs), or a
        // developer's own checkout in their folder
        let juce = Path::new(shell).join("JUCE");
        let version = &crate::pins::pins().juce.version;
        if juce.join("CMakeLists.txt").is_file() && juce.join("modules").is_dir() {
            let whose = if shell_source == "app" { format!("JUCE {version}, fetched by the app") } else { juce.display().to_string() };
            checks.push(Check::ok("juce", "JUCE", whose));
        } else if shell_source == "app" {
            checks.push(Check::missing("juce", "JUCE", "Not here yet.", "Pangea fetches it: press Get Pangea's tools."));
        } else {
            checks.push(Check::missing(
                "juce",
                "JUCE",
                format!("No JUCE checkout at {}.", juce.display()),
                format!("git clone --depth 1 --branch {version} https://github.com/juce-framework/JUCE.git \"{}\"", juce.display()),
            ));
        }
    }

    checks
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_faust_version_lines() {
        assert_eq!(parse_faust_version("FAUST Version 2.88.0\nEmbedded backends:"), Some("2.88.0".into()));
        assert_eq!(parse_faust_version("faust 2.79.3"), Some("2.79.3".into()));
        assert_eq!(parse_faust_version("nothing here"), None);
    }

    #[test]
    fn pinned_version_is_read_from_the_library_file() {
        assert!(pinned_faust_version().is_some());
    }

    #[test]
    fn a_members_build_never_shows_a_homebrew_command() {
        let member = carried_fix_for(false, "brew install faust");
        assert!(!member.contains("brew") && member.contains("Setup"), "{member}");
        assert!(carried_fix_for(true, "brew install faust").ends_with("brew install faust"));
    }
}
