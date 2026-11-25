//! Settings persisted with tauri-plugin-store in `settings.json` under the app's
//! config directory (`~/Library/Application Support/com.pangea.desktop/`).

use anyhow::{Context, Result};
use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Manager, Runtime};
use tauri_plugin_store::StoreExt;

use crate::toolchain::Toolchain;
use crate::AppState;

/// The web app a fresh install talks to. A build for download sets it with
/// `PANGEA_API_BASE_URL` (desktop/scripts/package-mac.sh); otherwise the local dev server.
pub const DEFAULT_API_BASE_URL: &str = match option_env!("PANGEA_API_BASE_URL") {
    Some(url) => url,
    None => "http://localhost:3000",
};
const STORE_FILE: &str = "settings.json";

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Settings {
    /// Base URL of the Pangea web app, without a trailing slash.
    pub api_base_url: String,
    /// A shell folder of the member's own (a developer's `shell/` in the repository), used in
    /// place of the app's own copy. Empty: the app's own (`toolchain.rs`).
    pub shell_dir: String,
}

/// Which shell builds use, and why.
#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct ShellInfo {
    /// `own` (a folder named in Settings), `app` (the app's own copy) or `none`.
    pub source: String,
    /// The folder, empty when there is none.
    pub dir: String,
    /// The shell's `VERSION` file (the commit the app was packaged from), when it has one.
    pub version: Option<String>,
}

/// The shell builds use: the member's own folder when Settings names one, which keeps a
/// developer's repository working as before; else the app's own copy; else none (a development
/// build carries no shell, and its default setting is the repository's).
pub fn shell_info(settings: &Settings, toolchain: Option<&Toolchain>) -> ShellInfo {
    let own = settings.shell_dir.trim();
    let (source, dir) = if !own.is_empty() {
        ("own", own.to_string())
    } else if let Some(t) = toolchain.filter(|t| t.shell_dir().join("build.sh").is_file()) {
        ("app", t.shell_dir().display().to_string())
    } else {
        ("none", String::new())
    };
    let version = (!dir.is_empty())
        .then(|| std::fs::read_to_string(std::path::Path::new(&dir).join("VERSION")).ok())
        .flatten()
        .map(|v| v.trim().to_string())
        .filter(|v| !v.is_empty());
    ShellInfo { source: source.into(), dir, version }
}

/// `shell_info(...).dir` for the running app.
pub fn shell_dir<R: Runtime>(app: &AppHandle<R>, settings: &Settings) -> String {
    let state = app.state::<AppState>();
    let toolchain = state.toolchain.lock().unwrap().clone();
    shell_info(settings, toolchain.as_ref()).dir
}

impl Settings {
    pub fn normalised(mut self) -> Self {
        self.api_base_url = normalise_base_url(&self.api_base_url);
        self.shell_dir = self.shell_dir.trim().to_string();
        if self.shell_dir.starts_with("~/") {
            if let Some(home) = std::env::var_os("HOME") {
                self.shell_dir = format!("{}/{}", home.to_string_lossy(), &self.shell_dir[2..]);
            }
        }
        self
    }
}

/// What a fresh install gets: no shell folder of its own, so the app's own copy is used. A
/// development build, which carries no shell, starts with the repository's `shell/`.
pub fn defaults() -> Settings {
    Settings {
        api_base_url: DEFAULT_API_BASE_URL.to_string(),
        shell_dir: default_shell_dir(),
    }
}

fn default_shell_dir() -> String {
    if cfg!(debug_assertions) {
        let repo_shell = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../shell");
        if let Ok(p) = repo_shell.canonicalize() {
            return p.to_string_lossy().to_string();
        }
    }
    String::new()
}

pub fn normalise_base_url(url: &str) -> String {
    let mut s = url.trim().to_string();
    if s.is_empty() {
        s = DEFAULT_API_BASE_URL.to_string();
    }
    if !s.starts_with("http://") && !s.starts_with("https://") {
        s = format!("https://{s}");
    }
    while s.ends_with('/') {
        s.pop();
    }
    s
}

pub fn load<R: Runtime>(app: &AppHandle<R>) -> Result<Settings> {
    let store = app.store(STORE_FILE).context("could not open the settings store")?;
    let d = defaults();
    let api_base_url = store
        .get("apiBaseUrl")
        .and_then(|v| v.as_str().map(str::to_string))
        .unwrap_or(d.api_base_url);
    let shell_dir = store
        .get("shellDir")
        .and_then(|v| v.as_str().map(str::to_string))
        .unwrap_or(d.shell_dir);
    Ok(Settings { api_base_url, shell_dir }.normalised())
}

pub fn save<R: Runtime>(app: &AppHandle<R>, settings: &Settings) -> Result<Settings> {
    let settings = settings.clone().normalised();
    let store = app.store(STORE_FILE).context("could not open the settings store")?;
    store.set("apiBaseUrl", serde_json::Value::String(settings.api_base_url.clone()));
    store.set("shellDir", serde_json::Value::String(settings.shell_dir.clone()));
    store.save().context("could not save settings")?;
    Ok(settings)
}

/// `~/Library/Application Support/Pangea/projects` (`paths::support_dir`).
pub fn projects_dir() -> Result<std::path::PathBuf> {
    Ok(crate::paths::support_dir()?.join("projects"))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::toolchain::{Manifest, ToolStamp};

    fn settings(shell_dir: &str) -> Settings {
        Settings { api_base_url: "https://pangea.example".into(), shell_dir: shell_dir.into() }
    }

    #[test]
    fn a_developers_own_shell_folder_comes_before_the_apps_copy() {
        let root = std::env::temp_dir().join(format!("pangea-settings-test-{}", std::process::id()));
        std::fs::create_dir_all(root.join("shell")).unwrap();
        std::fs::write(root.join("shell/build.sh"), "#!/bin/bash\n").unwrap();
        std::fs::write(root.join("shell/VERSION"), "abc123def456\n").unwrap();
        let t = Toolchain {
            root: root.clone(),
            manifest: Manifest {
                format: 1,
                app_version: "0.1.0".into(),
                commit: "abc123def456".into(),
                faust: ToolStamp { version: "2.88.0".into() },
                ninja: ToolStamp { version: "1.13.2".into() },
            },
        };

        let app = shell_info(&settings(""), Some(&t));
        assert_eq!(app.source, "app");
        assert_eq!(app.dir, root.join("shell").display().to_string());
        assert_eq!(app.version.as_deref(), Some("abc123def456"));

        let own = shell_info(&settings("/Users/someone/Pangea/shell"), Some(&t));
        assert_eq!((own.source.as_str(), own.dir.as_str()), ("own", "/Users/someone/Pangea/shell"));

        assert_eq!(shell_info(&settings("  "), None).source, "none");
        std::fs::remove_dir_all(&root).unwrap();
    }
}
