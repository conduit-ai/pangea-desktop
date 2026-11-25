//! The commands the page can invoke. Each returns `Result<T, String>` so the page
//! gets a plain message to show.

use tauri::{AppHandle, Manager, State};
use tauri_plugin_opener::OpenerExt;

use crate::api::{DeviceCodeResponse, Project};
use crate::apple::{self, AppleTools};
use crate::auth::AuthStatus;
use crate::build::{self, ConfirmRequest, InstallJob, QueueState};
use crate::deep_link::InstallRequest;
use crate::prereqs::Check;
use crate::settings::{Settings, ShellInfo};
use crate::toolchain::{self, About};
use crate::host_install::{self, HostPlugin, InstallReport};
use crate::selfcheck::{self, CheckReport};
use crate::setup::{self, SetupStatus};
use crate::tools::{self, Fetch, Needs, Report};
use std::sync::atomic::Ordering;
use tauri::Emitter;
use crate::{auth, prereqs, settings, AppState};

type CmdResult<T> = Result<T, String>;

fn err(e: anyhow::Error) -> String {
    format!("{e:#}")
}

#[tauri::command]
pub async fn get_settings(app: AppHandle) -> CmdResult<Settings> {
    settings::load(&app).map_err(err)
}

#[tauri::command]
pub async fn default_settings() -> CmdResult<Settings> {
    Ok(settings::defaults())
}

#[tauri::command]
pub async fn set_settings(app: AppHandle, state: State<'_, AppState>, settings: Settings) -> CmdResult<Settings> {
    let saved = settings::save(&app, &settings).map_err(err)?;
    // The shell may have changed: JUCE goes into the app's own, and the tools' path is set again.
    let shell = shell_info_now(&app, &state)?;
    tools::apply(&state, &shell).map_err(err)?;
    Ok(saved)
}

#[tauri::command]
pub async fn check_prerequisites(app: AppHandle, state: State<'_, AppState>) -> CmdResult<Vec<Check>> {
    let shell = shell_info_now(&app, &state)?;
    let apple = apple::current(&state).await;
    Ok(prereqs::check_all(&shell, &apple).await)
}

fn shell_info_now(app: &AppHandle, state: &AppState) -> CmdResult<ShellInfo> {
    let s = settings::load(app).map_err(err)?;
    let toolchain = state.toolchain.lock().map_err(|e| e.to_string())?.clone();
    Ok(settings::shell_info(&s, toolchain.as_ref()))
}

/// Which shell builds use: the app's own copy, or a developer's folder named in Settings.
#[tauri::command]
pub async fn shell_info(app: AppHandle, state: State<'_, AppState>) -> CmdResult<ShellInfo> {
    shell_info_now(&app, &state)
}

/// Each step of a fetch goes to the page as a `tool-fetch` event and is kept for a page that
/// opens part way.
struct EmitFetch<'a> {
    app: &'a AppHandle,
    state: &'a AppState,
}

impl Report for EmitFetch<'_> {
    fn report(&self, f: &Fetch) {
        if let Ok(mut last) = self.state.fetch_progress.lock() {
            *last = Some(f.clone());
        }
        let _ = self.app.emit(tools::FETCH_EVENT, f);
    }
}

/// Fetches what builds still need (CMake, JUCE), checks it and puts it in place.
#[tauri::command]
pub async fn get_tools(app: AppHandle, state: State<'_, AppState>) -> CmdResult<Needs> {
    if state.fetching.swap(true, Ordering::SeqCst) {
        return Err("Pangea is already fetching its tools.".into());
    }
    let result = match shell_info_now(&app, &state) {
        Ok(shell) => tools::ensure(&state, &shell, &EmitFetch { app: &app, state: &state }).await.map_err(err),
        Err(e) => Err(e),
    };
    state.fetching.store(false, Ordering::SeqCst);
    result
}

/// What builds still need fetched, without fetching it, and the last progress of a fetch.
#[tauri::command]
pub async fn tools_needs(app: AppHandle, state: State<'_, AppState>) -> CmdResult<(Needs, Option<Fetch>)> {
    let shell = shell_info_now(&app, &state)?;
    let support = crate::paths::support_dir().map_err(err)?;
    let last = state.fetch_progress.lock().map_err(|e| e.to_string())?.clone();
    Ok((tools::needs(&support, &shell), last))
}

/// Apple's build tools, checked again (a program must build and run).
#[tauri::command]
pub async fn apple_tools(state: State<'_, AppState>) -> CmdResult<AppleTools> {
    Ok(apple::refresh(&state).await)
}

/// Starts Apple's own installer for the Command Line Tools.
#[tauri::command]
pub async fn apple_tools_install(state: State<'_, AppState>) -> CmdResult<AppleTools> {
    Ok(apple::install(&state).await)
}

/// Opens Xcode, so the member can accept its licence.
#[tauri::command]
pub async fn open_xcode(app: AppHandle, state: State<'_, AppState>) -> CmdResult<()> {
    let known = state.apple.lock().map_err(|e| e.to_string())?.clone();
    let xcode = known
        .and_then(|t| t.developer_dir)
        .and_then(|d| d.strip_suffix("/Contents/Developer").map(str::to_string))
        .filter(|d| d.ends_with(".app"))
        .unwrap_or_else(|| "/Applications/Xcode.app".to_string());
    app.opener().open_path(xcode, None::<&str>).map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn auth_status(app: AppHandle) -> CmdResult<AuthStatus> {
    auth::status(&app).await.map_err(err)
}

#[tauri::command]
pub async fn sign_in_start(app: AppHandle) -> CmdResult<DeviceCodeResponse> {
    auth::start(&app).await.map_err(err)
}

#[tauri::command]
pub async fn sign_in_wait(app: AppHandle, device_code: String, interval: u64, expires_in: u64) -> CmdResult<AuthStatus> {
    auth::wait(&app, &device_code, interval, expires_in).await.map_err(err)
}

#[tauri::command]
pub async fn sign_in_cancel(app: AppHandle) -> CmdResult<()> {
    auth::cancel(&app);
    Ok(())
}

#[tauri::command]
pub async fn sign_out(app: AppHandle) -> CmdResult<()> {
    auth::sign_out(&app).map_err(err)
}

#[tauri::command]
pub async fn list_projects(app: AppHandle) -> CmdResult<Vec<Project>> {
    let api = auth::signed_in_client(&app).map_err(err)?;
    api.projects().await.map_err(err)
}

#[tauri::command]
pub async fn open_project_on_web(app: AppHandle, project_id: String) -> CmdResult<()> {
    let s = settings::load(&app).map_err(err)?;
    let url = format!("{}/projects/{}", s.api_base_url, project_id);
    app.opener().open_url(url, None::<&str>).map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn open_url(app: AppHandle, url: String) -> CmdResult<()> {
    if !(url.starts_with("http://") || url.starts_with("https://")) {
        return Err("only http and https links can be opened".into());
    }
    app.opener().open_url(url, None::<&str>).map_err(|e| e.to_string())
}

/// Queues an install of a project version (the head version when `version` is omitted).
#[tauri::command]
pub async fn build_and_install(
    app: AppHandle,
    state: State<'_, AppState>,
    project_id: String,
    version: Option<u32>,
    project_name: Option<String>,
    source: Option<String>,
) -> CmdResult<InstallJob> {
    crate::api::validate_project_id(project_id.trim()).map_err(err)?;
    if let Some(n) = version {
        crate::api::validate_version(n).map_err(err)?;
    }
    Ok(build::request(
        &app,
        state.build.clone(),
        project_id.trim(),
        version,
        project_name,
        source.as_deref().unwrap_or("ui"),
    ))
}

#[tauri::command]
pub async fn build_state(state: State<'_, AppState>) -> CmdResult<QueueState> {
    Ok(state.build.state())
}

/// The confirmation the build worker is waiting for, if any (asked again after a page reload).
#[tauri::command]
pub async fn pending_confirm(state: State<'_, AppState>) -> CmdResult<Option<ConfirmRequest>> {
    Ok(build::pending_confirm(&state))
}

/// Install (true) or Cancel (false) for the pending confirmation.
#[tauri::command]
pub async fn confirm_install(state: State<'_, AppState>, job_id: u64, ok: bool) -> CmdResult<bool> {
    Ok(build::answer_confirm(&state, job_id, ok))
}

#[tauri::command]
pub async fn clear_build_history(state: State<'_, AppState>) -> CmdResult<QueueState> {
    state.build.clear_history();
    Ok(state.build.state())
}

/// Deep links that arrived before the page was listening. Each is returned once.
#[tauri::command]
pub async fn take_pending_installs(state: State<'_, AppState>) -> CmdResult<Vec<InstallRequest>> {
    let mut pending = state.pending_installs.lock().map_err(|e| e.to_string())?;
    Ok(std::mem::take(&mut *pending))
}

/// The app's version and what it carries, for Settings' About section.
#[tauri::command]
pub async fn about(state: State<'_, AppState>) -> CmdResult<About> {
    Ok(toolchain::about(&state))
}

/// Opens the folder with the licences of the tools the app carries and Faust's source.
#[tauri::command]
pub async fn open_licences(app: AppHandle, state: State<'_, AppState>) -> CmdResult<()> {
    let dir = toolchain::about(&state).licences_dir.ok_or_else(|| "This build of the app carries no tools.".to_string())?;
    app.opener().open_path(dir, None::<&str>).map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn projects_dir() -> CmdResult<String> {
    settings::projects_dir().map(|p| p.display().to_string()).map_err(err)
}

/// The Pangea plugin on this Mac (host_install.rs), and the DAWs open now.
#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
pub struct HostPluginStatus {
    pub plugin: HostPlugin,
    pub daws: Vec<String>,
    pub installing: bool,
}

#[tauri::command]
pub async fn host_plugin(app: AppHandle, state: State<'_, AppState>) -> CmdResult<HostPluginStatus> {
    let shell = shell_info_now(&app, &state)?;
    let plugins = crate::paths::plugins_dir().map_err(err)?;
    let support = crate::paths::support_dir().map_err(err)?;
    Ok(HostPluginStatus {
        plugin: host_install::detect(&plugins, std::path::Path::new(&shell.dir), &support),
        daws: host_install::running_daws(),
        installing: state.installing_host.load(Ordering::SeqCst),
    })
}

/// Lines and stages of an install, to the page as events.
struct EmitInstall<'a> {
    app: &'a AppHandle,
}

impl crate::build::JobLog for EmitInstall<'_> {
    fn push(&self, stream: &str, line: &str) {
        let _ = self.app.emit(host_install::LOG_EVENT, (stream, line));
    }
}

impl host_install::Stages for EmitInstall<'_> {
    fn stage(&self, stage: &str) {
        let _ = self.app.emit(host_install::STAGE_EVENT, stage);
    }
}

/// Builds, installs and checks the Pangea plugin. Unless `anyway`, it stops first when a DAW
/// is open, and says which.
#[tauri::command]
pub async fn install_host_plugin(app: AppHandle, state: State<'_, AppState>, anyway: bool) -> CmdResult<InstallReport> {
    let daws = host_install::running_daws();
    if !anyway && !daws.is_empty() {
        return Ok(host_install::close_daws_first(&daws));
    }
    // Pangea's tools first: without them build-host.sh would stop at its first check, in
    // Terminal's words.
    let shell = shell_info_now(&app, &state)?;
    let support = crate::paths::support_dir().map_err(err)?;
    if let Some(first) = host_install::tools_first(&tools::needs(&support, &shell), shell.source != "none") {
        return Ok(first);
    }
    if state.installing_host.swap(true, Ordering::SeqCst) {
        return Err("The Pangea plugin is being installed already.".into());
    }
    let result = match (shell_info_now(&app, &state), crate::paths::plugins_dir(), crate::paths::support_dir()) {
        (Ok(shell), Ok(plugins), Ok(support)) => {
            let log = EmitInstall { app: &app };
            Ok(host_install::install(std::path::Path::new(&shell.dir), &plugins, &support, &log, &log).await)
        }
        (Err(e), _, _) => Err(e),
        (_, Err(e), _) | (_, _, Err(e)) => Err(err(e)),
    };
    if let Ok(r) = &result {
        *state.last_install.lock().map_err(|e| e.to_string())? = Some(r.clone());
    }
    state.installing_host.store(false, Ordering::SeqCst);
    result
}

/// Lines and stages of the self-check, to the page as events.
struct EmitCheck<'a> {
    app: &'a AppHandle,
}

impl crate::build::JobLog for EmitCheck<'_> {
    fn push(&self, stream: &str, line: &str) {
        let _ = self.app.emit(selfcheck::LOG_EVENT, (stream, line));
    }
}

impl host_install::Stages for EmitCheck<'_> {
    fn stage(&self, stage: &str) {
        let _ = self.app.emit(selfcheck::STAGE_EVENT, stage);
    }
}

/// The self-check: a test plugin built and opened by the Pangea plugin (selfcheck.rs).
#[tauri::command]
pub async fn run_self_check(app: AppHandle, state: State<'_, AppState>) -> CmdResult<CheckReport> {
    if state.checking.swap(true, Ordering::SeqCst) {
        return Err("The check is running already.".into());
    }
    let result = match (shell_info_now(&app, &state), crate::paths::plugins_dir(), crate::paths::support_dir()) {
        (Ok(shell), Ok(plugins), Ok(support)) => {
            let log = EmitCheck { app: &app };
            Ok(selfcheck::run(std::path::Path::new(&shell.dir), &plugins, &support, &log, &log).await)
        }
        (Err(e), _, _) => Err(e),
        (_, Err(e), _) | (_, _, Err(e)) => Err(err(e)),
    };
    if let Ok(r) = &result {
        *state.last_check.lock().map_err(|e| e.to_string())? = Some(r.clone());
    }
    state.checking.store(false, Ordering::SeqCst);
    result
}

/// Unpacks what the app carries again, after it failed at start (Setup's Try again).
#[tauri::command]
pub async fn retry_unpack(app: AppHandle, state: State<'_, AppState>) -> CmdResult<()> {
    let resources = app.path().resource_dir().ok();
    let exe_dir = std::env::current_exe().ok().and_then(|p| p.parent().map(std::path::Path::to_path_buf));
    let unpacked = toolchain::prepare(&state, resources.as_deref(), exe_dir.as_deref());
    if unpacked.is_ok() {
        let shell = shell_info_now(&app, &state)?;
        tools::apply(&state, &shell).map_err(err)?;
    }
    Ok(())
}

/// The Setup screen: five steps, their states and the main button (setup.rs).
#[tauri::command]
pub async fn setup_status(app: AppHandle, state: State<'_, AppState>) -> CmdResult<SetupStatus> {
    // The unpack that starts with the app decides which shell there is: wait for it a little,
    // and while it still runs the tools step says it is getting ready.
    toolchain::wait_for_unpack(&state, std::time::Duration::from_secs(10)).await;
    let shell = shell_info_now(&app, &state)?;
    let signed_in = auth::session(&app).map(|s| s.1.is_some()).unwrap_or(false);
    Ok(setup::plan(&setup::gather(&state, shell, signed_in).await))
}
