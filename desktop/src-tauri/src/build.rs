//! The install flow: fetch a version, write the project directory, create the
//! `native_builds` row, run `build.sh --install` with its output streamed to the
//! page, report the result. One build at a time; further requests queue.
//!
//! A MIDI tool (`kind: "midi"`, docs/midi-tools.md) installs as two plugins: `<name>`,
//! an instrument with MIDI out (AU and VST3) for Ableton Live and Bitwig, and
//! `<name> FX`, an AU MIDI effect for Logic. `build.sh` builds, installs and validates
//! both; the result and the log name both.

use std::collections::VecDeque;
use std::ffi::OsString;
use std::path::{Path, PathBuf};
use std::process::Stdio;
use std::sync::{Arc, Mutex};

use anyhow::{anyhow, bail, Context, Result};
use serde::Serialize;
use serde_json::Value;
use tauri::{AppHandle, Emitter, Manager, Runtime};
use tokio::io::{AsyncBufReadExt, BufReader};

use crate::api::{Api, CreateBuild, Owner, PatchBuild, VersionBundle};
use crate::{auth, deep_link, prereqs, safety, settings, AppState};

pub const LOG_EVENT: &str = "build-log";
pub const STATE_EVENT: &str = "build-state";
pub const CONFIRM_EVENT: &str = "confirm-request";
pub const LOGIC_POLICY_MESSAGE: &str = "Logic modules from other members are not built yet. The sound part of this project is Faust and safe to build; logic modules will be sandboxed in a later version.";
/// A MIDI tool is all logic module, so the Faust half of the message above does not apply.
pub const MIDI_TOOL_POLICY_MESSAGE: &str = "MIDI tools from other members are not built yet. A MIDI tool is a logic module, which is plain C++; logic modules will be sandboxed in a later version.";

/// What the page shows before someone else's code is compiled.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ConfirmRequest {
    pub job_id: u64,
    pub project_name: String,
    pub version: u32,
    pub owner: String,
    pub owned_by_me: bool,
    pub from_link: bool,
    pub has_logic: bool,
    /// `effect`, `instrument` or `midi` (a MIDI tool, which is all logic module).
    pub kind: String,
}
pub const INSTALLED_MESSAGE: &str =
    "Installed into Logic. Restart Logic or rescan plug-ins if it does not appear.";

/// What the result says once a project is installed. A MIDI tool says where each of its two plugins goes.
pub fn installed_message(kind: &str, name: &str) -> String {
    if kind == "midi" {
        format!(
            "Installed. In Logic, choose {name} FX in a MIDI FX slot. In Ableton Live or Bitwig, put {name} on a MIDI track. Restart Logic or rescan plug-ins if it does not appear."
        )
    } else {
        INSTALLED_MESSAGE.to_string()
    }
}

/// Why a project must not be built for this user, if it must not: a logic module (and so every
/// MIDI tool) is compiled only for the project's owner, until logic modules are sandboxed.
pub fn policy_refusal(kind: &str, has_logic: bool, mine: bool) -> Option<&'static str> {
    if mine || (!has_logic && kind != "midi") {
        return None;
    }
    Some(if kind == "midi" { MIDI_TOOL_POLICY_MESSAGE } else { LOGIC_POLICY_MESSAGE })
}
const HISTORY_LIMIT: usize = 20;

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct InstallJob {
    pub id: u64,
    pub project_id: String,
    /// `None` means the project's head version.
    pub version: Option<u32>,
    pub project_name: Option<String>,
    /// `ui` (a button in the app) or `link` (a `pangea://` deep link).
    pub source: String,
    pub queued_at: String,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct JobResult {
    pub id: u64,
    pub project_id: String,
    pub project_name: String,
    pub version: Option<u32>,
    pub ok: bool,
    /// `installed`, `failed`, `refused` (the safety check said no) or `cancelled` (the user said no).
    pub outcome: String,
    pub message: String,
    pub log: String,
    pub component: Option<String>,
    /// A MIDI tool's second plugin, the AU MIDI effect for Logic.
    pub component_fx: Option<String>,
    pub vst3: Option<String>,
    pub seconds: Option<f64>,
    pub build_id: Option<String>,
    pub project_dir: Option<String>,
    pub finished_at: String,
}

#[derive(Debug, Clone, Serialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct QueueState {
    pub current: Option<InstallJob>,
    pub queue: Vec<InstallJob>,
    pub history: Vec<JobResult>,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LogLine {
    pub job_id: u64,
    /// `stdout`, `stderr` or `app` (lines written by the app itself).
    pub stream: String,
    pub line: String,
}

#[derive(Default)]
struct Inner {
    next_id: u64,
    running: bool,
    current: Option<InstallJob>,
    queue: VecDeque<InstallJob>,
    history: Vec<JobResult>,
}

#[derive(Default)]
pub struct BuildQueue {
    inner: Mutex<Inner>,
}

impl BuildQueue {
    pub fn state(&self) -> QueueState {
        let g = self.inner.lock().unwrap();
        QueueState { current: g.current.clone(), queue: g.queue.iter().cloned().collect(), history: g.history.clone() }
    }

    pub fn clear_history(&self) {
        self.inner.lock().unwrap().history.clear();
    }

    /// Adds a job; returns it and whether a worker must be started.
    fn enqueue(&self, project_id: &str, version: Option<u32>, project_name: Option<String>, source: &str) -> (InstallJob, bool) {
        let mut g = self.inner.lock().unwrap();
        g.next_id += 1;
        let job = InstallJob {
            id: g.next_id,
            project_id: project_id.to_string(),
            version,
            project_name,
            source: source.to_string(),
            queued_at: now(),
        };
        g.queue.push_back(job.clone());
        let start = !g.running;
        if start {
            g.running = true;
        }
        (job, start)
    }

    fn take_next(&self) -> Option<InstallJob> {
        let mut g = self.inner.lock().unwrap();
        match g.queue.pop_front() {
            Some(job) => {
                g.current = Some(job.clone());
                Some(job)
            }
            None => {
                g.current = None;
                g.running = false;
                None
            }
        }
    }

    fn finish(&self, result: JobResult) {
        let mut g = self.inner.lock().unwrap();
        g.current = None;
        g.history.insert(0, result);
        g.history.truncate(HISTORY_LIMIT);
    }
}

fn now() -> String {
    chrono::Local::now().to_rfc3339_opts(chrono::SecondsFormat::Secs, true)
}

fn emit_state<R: Runtime>(app: &AppHandle<R>, q: &BuildQueue) {
    let _ = app.emit(STATE_EVENT, q.state());
}

/// Queues an install and starts the worker if it is idle.
pub fn request<R: Runtime>(
    app: &AppHandle<R>,
    queue: Arc<BuildQueue>,
    project_id: &str,
    version: Option<u32>,
    project_name: Option<String>,
    source: &str,
) -> InstallJob {
    let (job, start) = queue.enqueue(project_id, version, project_name, source);
    emit_state(app, &queue);
    if start {
        let app = app.clone();
        tauri::async_runtime::spawn(async move {
            while let Some(job) = queue.take_next() {
                emit_state(&app, &queue);
                let result = run_job(&app, &job).await;
                queue.finish(result);
                emit_state(&app, &queue);
            }
        });
    }
    job
}

/// Where the shared steps (the checks on disk, the build script) write their lines: an
/// install job's log on the page, or stderr for the Pangea plugin's jobs (`host.rs`).
pub trait JobLog: Send + Sync {
    /// `stream` is `stdout`, `stderr` or `app` (lines written by the app itself).
    fn push(&self, stream: &str, line: &str);
    fn app(&self, line: &str) {
        self.push("app", line);
    }
}

/// Collects the log and streams each line to the page.
struct Log<'a, R: Runtime> {
    app: &'a AppHandle<R>,
    job_id: u64,
    lines: Arc<Mutex<Vec<String>>>,
}

impl<'a, R: Runtime> Log<'a, R> {
    fn push(&self, stream: &str, line: &str) {
        self.lines.lock().unwrap().push(line.to_string());
        let _ = self.app.emit(LOG_EVENT, LogLine { job_id: self.job_id, stream: stream.into(), line: line.into() });
    }
    fn app(&self, line: impl AsRef<str>) {
        self.push("app", line.as_ref());
    }
    fn text(&self) -> String {
        self.lines.lock().unwrap().join("\n")
    }
}

impl<R: Runtime> JobLog for Log<'_, R> {
    fn push(&self, stream: &str, line: &str) {
        Log::push(self, stream, line);
    }
}

async fn run_job<R: Runtime>(app: &AppHandle<R>, job: &InstallJob) -> JobResult {
    let lines = Arc::new(Mutex::new(Vec::new()));
    let log = Log { app, job_id: job.id, lines: lines.clone() };
    let mut outcome = Outcome::default();
    let name = match run_steps(app, job, &log, &mut outcome).await {
        Ok(name) => name,
        Err(e) => {
            let msg = format!("{e:#}");
            if outcome.outcome.is_empty() {
                log.app(format!("error: {msg}"));
                outcome.outcome = "failed".into();
            }
            outcome.ok = false;
            if outcome.message.is_empty() {
                outcome.message = msg;
            }
            outcome.name.clone().or_else(|| job.project_name.clone()).unwrap_or_else(|| job.project_id.clone())
        }
    };
    JobResult {
        id: job.id,
        project_id: job.project_id.clone(),
        project_name: name,
        version: outcome.version.or(job.version),
        ok: outcome.ok,
        outcome: outcome.outcome,
        message: outcome.message,
        log: log.text(),
        component: outcome.component,
        component_fx: outcome.component_fx,
        vst3: outcome.vst3,
        seconds: outcome.seconds,
        build_id: outcome.build_id,
        project_dir: outcome.project_dir,
        finished_at: now(),
    }
}

#[derive(Default)]
struct Outcome {
    ok: bool,
    outcome: String,
    message: String,
    name: Option<String>,
    version: Option<u32>,
    component: Option<String>,
    component_fx: Option<String>,
    vst3: Option<String>,
    seconds: Option<f64>,
    build_id: Option<String>,
    project_dir: Option<String>,
}

/// The steps in order. Any error becomes a failed result with the log so far.
async fn run_steps<R: Runtime>(
    app: &AppHandle<R>,
    job: &InstallJob,
    log: &Log<'_, R>,
    out: &mut Outcome,
) -> Result<String> {
    let cfg = settings::load(app)?;
    let api = auth::signed_in_client(app).map_err(|_| anyhow!("Sign in before installing."))?;
    let shell_dir = settings::shell_dir(app, &cfg);
    let script = Path::new(&shell_dir).join("build.sh");
    if shell_dir.trim().is_empty() || !script.is_file() {
        bail!("Pangea has no shell to build with ({}). Open Setup in the Pangea app.", if shell_dir.trim().is_empty() { "none was found" } else { shell_dir.as_str() });
    }

    // 1. Which version, and whose project it is
    log.app(format!("Looking up {} at {}", job.project_name.clone().unwrap_or_else(|| job.project_id.clone()), api.base()));
    let listed = api.projects().await?.into_iter().find(|p| p.id == job.project_id);
    let number = match job.version {
        Some(n) => n,
        None => {
            let p = listed.as_ref().ok_or_else(|| anyhow!("Project {} is not in your list.", job.project_id))?;
            p.head_version.ok_or_else(|| anyhow!("{} has no version to build yet.", p.name))?
        }
    };
    out.version = Some(number);
    let name = listed.as_ref().map(|p| p.name.clone()).or_else(|| job.project_name.clone());

    // 2. Fetch the version
    log.app(format!("Fetching {} v{number}", name.clone().unwrap_or_else(|| job.project_id.clone())));
    let bundle = api.version(&job.project_id, number).await?;
    let display_name = bundle
        .plugin
        .get("name")
        .and_then(|n| n.as_str())
        .map(str::to_string)
        .or(name)
        .unwrap_or_else(|| bundle.slug.clone());
    out.name = Some(display_name.clone());
    let kind = bundle.plugin.get("kind").and_then(|k| k.as_str()).unwrap_or("effect").to_string();

    // 3. Confirm when the project is someone else's, and always for a link
    let owner = if !bundle.owner.is_empty() {
        bundle.owner.clone()
    } else {
        listed.as_ref().map(|p| p.owner_details.clone()).unwrap_or_default()
    };
    let mine = confirm_if_needed(app, &api, job, &display_name, &bundle, &owner, log, out).await?;

    // 4. Check the sources and the interface in memory, before anything touches the disk.
    //    A refusal is still recorded as a failed build on the web side. The interface is
    //    checked exactly as it will be written; a version without one gets no ui.json.
    //    The policy is decided here too: a logic module (plain C++) is compiled only for the
    //    project's own owner, and another member's logic module is never written to disk.
    log.app("Checking the sources before writing them");
    let (in_memory, ui_text) = check_in_memory(&bundle);
    for v in &in_memory {
        log.push("stderr", &v.to_string());
    }
    let early = if !in_memory.is_empty() {
        Some(refusal_reason(&in_memory))
    } else {
        policy_refusal(&kind, bundle.logic.is_some(), mine).map(|p| {
            log.push("stderr", p);
            p.to_string()
        })
    };
    let dir = project_dir(&settings::projects_dir()?, &bundle);
    let machine = machine_string().await;
    if let Some(reason) = early {
        log.app("Refused: nothing was written or built.");
        let build_id = api
            .create_build(&CreateBuild { project_id: &job.project_id, version_id: &bundle.version_id, target: "both", machine: &machine })
            .await?;
        out.build_id = Some(build_id.clone());
        let log_text = log.text();
        let patch = PatchBuild { status: "failed", log: Some(&log_text), faust_version: None, shell_version: None };
        if let Err(e) = api.patch_build(&build_id, &patch).await {
            log.app(format!("Could not report the build to Pangea: {e:#}"));
        }
        out.outcome = "refused".into();
        out.message = format!("Not built: {reason}");
        bail!("{}", out.message);
    }

    // 5. Write the project directory
    write_project(&dir, &bundle, ui_text.as_deref())?;
    out.project_dir = Some(dir.display().to_string());
    log.app(format!("Wrote {}", dir.display()));

    // 6. Create the build row
    let build_id = api
        .create_build(&CreateBuild {
            project_id: &job.project_id,
            version_id: &bundle.version_id,
            target: "both",
            machine: &machine,
        })
        .await?;
    out.build_id = Some(build_id.clone());
    log.app(format!("Build {build_id} created ({machine})"));
    if let Err(e) = api.patch_build(&build_id, &PatchBuild { status: "running", log: None, faust_version: None, shell_version: None }).await {
        log.app(format!("Could not mark the build as running: {e:#}"));
    }

    // 7. The checks on disk: the Rust checker again on the written files, then the shell's
    //    check-project.sh (the policy was decided before anything was written). A refusal is
    //    reported as a failed build, the project directory is removed and nothing is compiled.
    let refusal = safety_check(&shell_dir, &dir, log).await;
    if let Some(reason) = refusal {
        remove_refused(&dir, log);
        out.project_dir = None;
        log.app("Refused: nothing was built.");
        let log_text = log.text();
        let patch = PatchBuild { status: "failed", log: Some(&log_text), faust_version: None, shell_version: None };
        if let Err(e) = api.patch_build(&build_id, &patch).await {
            log.app(format!("Could not report the build to Pangea: {e:#}"));
        }
        out.outcome = "refused".into();
        out.message = format!("Not built: {reason}");
        bail!("{}", out.message);
    }

    // 7. Run build.sh --install. `--allow-logic` goes only with the user's own projects, so
    //    the shell refuses a logic module from another member too (both layers hold).
    let allow_logic = mine && bundle.logic.is_some();
    log.app(format!(
        "$ bash {} {} --install{}",
        script.display(),
        dir.display(),
        if allow_logic { " --allow-logic" } else { "" }
    ));
    let mut args = vec![dir.clone().into_os_string(), "--install".into()];
    if allow_logic {
        args.push("--allow-logic".into());
    }
    let (status_ok, summary) = run_script(&script, &args, &dir, log).await?;

    // 8. Judge and report
    let summary_ok = summary.as_ref().and_then(|s| s.get("ok")).and_then(|v| v.as_bool());
    let ok = status_ok && summary_ok.unwrap_or(true);
    let faust_version = summary.as_ref().and_then(|s| str_field(s, &["faust", "faustVersion"]));
    let faust_version = match faust_version {
        Some(v) => Some(v),
        None => faust_version_from_tool().await,
    };
    let shell_version = match summary.as_ref().and_then(|s| str_field(s, &["shell", "shellVersion"])) {
        Some(v) => Some(v),
        None => match shell_version_from_dir(&shell_dir).await {
            Some(v) => Some(v),
            None => summary.as_ref().and_then(|s| str_field(s, &["juce"])).map(|j| format!("juce {j}")),
        },
    };
    if let Some(s) = summary.as_ref() {
        let installed = Installed::from_summary(s);
        out.component = installed.component;
        out.component_fx = installed.component_fx;
        out.vst3 = installed.vst3;
    }
    out.seconds = summary.as_ref().and_then(|s| s.get("seconds")).and_then(|v| v.as_f64());
    out.ok = ok;
    out.outcome = if ok { "installed" } else { "failed" }.into();
    out.message = if ok {
        installed_message(&kind, &display_name)
    } else {
        let why = summary
            .as_ref()
            .and_then(|s| str_field(s, &["error", "message"]))
            .unwrap_or_else(|| "see the log below".to_string());
        format!("The build failed: {why}")
    };
    if ok {
        // Everything that was installed, one line each (both plugins of a MIDI tool), with auval's verdicts.
        let auval = |key: &str| summary.as_ref().and_then(|s| str_field(s, &[key])).unwrap_or_else(|| "skipped".into());
        if let Some(c) = &out.component {
            log.app(format!("Installed {c} (auval {})", auval("auval")));
        }
        if let Some(c) = &out.component_fx {
            log.app(format!("Installed {c} (auval {})", auval("auvalFx")));
        }
        if let Some(v) = &out.vst3 {
            log.app(format!("Installed {v}"));
        }
    }
    log.app(if ok { "Done." } else { "Failed." });

    let log_text = log.text();
    let patch = PatchBuild {
        status: if ok { "done" } else { "failed" },
        log: Some(&log_text),
        faust_version: faust_version.as_deref(),
        shell_version: shell_version.as_deref(),
    };
    if let Err(e) = api.patch_build(&build_id, &patch).await {
        log.app(format!("Could not report the build to Pangea: {e:#}"));
    } else {
        log.app(format!("Reported build {build_id} as {}", patch.status));
    }
    Ok(display_name)
}

/// Asks before compiling someone else's code, and before anything that a link started.
/// The question goes to the page as a `confirm-request` event and comes back through
/// the `confirm_install` command. Returns whether the signed-in user owns the project;
/// returns an error (with `out.outcome = "cancelled"`) when the user says no.
#[allow(clippy::too_many_arguments)]
async fn confirm_if_needed<R: Runtime>(
    app: &AppHandle<R>,
    api: &Api,
    job: &InstallJob,
    name: &str,
    bundle: &VersionBundle,
    owner: &Owner,
    log: &Log<'_, R>,
    out: &mut Outcome,
) -> Result<bool> {
    let me = api.me().await?;
    let mine = owner.is(&me);
    let from_link = job.source == "link";
    if mine && !from_link {
        return Ok(true);
    }
    let who = if mine { format!("you ({})", me.email) } else { owner.display() };
    log.app(format!("Waiting for you to confirm the install of {name} v{} by {who}", bundle.number));
    deep_link::bring_forward(app);
    let request = ConfirmRequest {
        job_id: job.id,
        project_name: name.to_string(),
        version: bundle.number,
        owner: who,
        owned_by_me: mine,
        from_link,
        has_logic: bundle.logic.is_some(),
        kind: bundle.plugin.get("kind").and_then(|k| k.as_str()).unwrap_or("effect").to_string(),
    };
    let (tx, rx) = tokio::sync::oneshot::channel::<bool>();
    {
        let state = app.state::<AppState>();
        let mut pending = state.pending_confirm.lock().unwrap();
        *pending = Some((request.clone(), tx));
    }
    let _ = app.emit(CONFIRM_EVENT, &request);
    let ok = rx.await.unwrap_or(false);
    if ok {
        log.app("Confirmed.");
        Ok(mine)
    } else {
        out.outcome = "cancelled".into();
        out.message = "Cancelled. Nothing was built.".into();
        log.app("Cancelled. Nothing was built.");
        bail!("{}", out.message)
    }
}

/// The page's answer to the pending confirmation. Returns false when nothing was pending.
pub fn answer_confirm(state: &AppState, job_id: u64, ok: bool) -> bool {
    let mut pending = state.pending_confirm.lock().unwrap();
    match pending.take() {
        Some((req, tx)) if req.job_id == job_id => {
            let _ = tx.send(ok);
            true
        }
        Some(other) => {
            *pending = Some(other);
            false
        }
        None => false,
    }
}

pub fn pending_confirm(state: &AppState) -> Option<ConfirmRequest> {
    state.pending_confirm.lock().unwrap().as_ref().map(|(r, _)| r.clone())
}

/// The checks on a fetched version before anything touches the disk: the sources, and the
/// interface exactly as it will be written. Returns what is wrong (empty when nothing is) and
/// the text of `ui.json`, which is `None` for a version without an interface.
pub(crate) fn check_in_memory(bundle: &VersionBundle) -> (Vec<safety::Violation>, Option<String>) {
    let mut found = safety::check_sources(&bundle.faust, bundle.logic.as_deref());
    let ui_text = safety::ui_json_text(&bundle.ui);
    if let Some(text) = &ui_text {
        found.extend(safety::check_ui_json(text));
    }
    (found, ui_text)
}

/// The first problem, and how many more there are. `found` must not be empty.
pub(crate) fn refusal_reason(found: &[safety::Violation]) -> String {
    let first = found[0].to_string();
    if found.len() > 1 {
        format!("{first} (and {} more)", found.len() - 1)
    } else {
        first
    }
}

/// `<projects>/<slug>/v<N>`, where a version is written before it is built.
pub(crate) fn project_dir(projects: &Path, bundle: &VersionBundle) -> PathBuf {
    projects.join(&bundle.slug).join(format!("v{}", bundle.number))
}

/// Removes a project directory the checks refused, so nothing refused stays on disk.
pub(crate) fn remove_refused(dir: &Path, log: &dyn JobLog) {
    match std::fs::remove_dir_all(dir) {
        Ok(()) => log.app(&format!("Removed {}", dir.display())),
        Err(e) => log.app(&format!("Could not remove {}: {e}", dir.display())),
    }
}

/// Runs the checks; returns the reason when the project must not be built.
pub(crate) async fn safety_check(shell_dir: &str, dir: &Path, log: &dyn JobLog) -> Option<String> {
    log.app("Checking the sources before building");
    match safety::check_project_dir(dir) {
        Ok(v) if v.is_empty() => {}
        Ok(v) => {
            for violation in &v {
                log.push("stderr", &violation.to_string());
            }
            return Some(refusal_reason(&v));
        }
        Err(e) => return Some(format!("could not read the project files: {e:#}")),
    }

    let script = Path::new(shell_dir).join("check-project.sh");
    if !script.is_file() {
        log.app("No check-project.sh in the shell yet; the app's own check passed.");
        return None;
    }
    log.app(&format!("$ bash {} {}", script.display(), dir.display()));
    let out = prereqs::command("bash").arg(&script).arg(dir).output().await;
    match out {
        Ok(o) => {
            let stdout = String::from_utf8_lossy(&o.stdout);
            let stderr = String::from_utf8_lossy(&o.stderr);
            for l in stdout.lines() {
                log.push("stdout", l);
            }
            for l in stderr.lines() {
                log.push("stderr", l);
            }
            if o.status.success() {
                None
            } else {
                let reason = stderr
                    .lines()
                    .chain(stdout.lines())
                    .map(str::trim)
                    .rfind(|l| !l.is_empty())
                    .unwrap_or("check-project.sh refused the project")
                    .to_string();
                Some(reason)
            }
        }
        Err(e) => Some(format!("could not run check-project.sh: {e}")),
    }
}

/// Writes the project files. `ui` is the checked text of ui.json, or `None` when the version
/// carries no interface, in which case any old ui.json is removed and the plugin gets the default look.
pub(crate) fn write_project(dir: &Path, b: &VersionBundle, ui: Option<&str>) -> Result<()> {
    std::fs::create_dir_all(dir).with_context(|| format!("could not create {}", dir.display()))?;
    let plugin = serde_json::to_string_pretty(&b.plugin)?;
    std::fs::write(dir.join("plugin.json"), plugin)?;
    std::fs::write(dir.join("dsp.dsp"), &b.faust)?;
    let logic_path = dir.join("logic.cpp");
    match &b.logic {
        Some(src) => std::fs::write(&logic_path, src)?,
        None => {
            if logic_path.exists() {
                std::fs::remove_file(&logic_path)?;
            }
        }
    }
    let ui_path = dir.join("ui.json");
    match ui {
        Some(text) => std::fs::write(&ui_path, text)?,
        None => {
            if ui_path.exists() {
                std::fs::remove_file(&ui_path)?;
            }
        }
    }
    Ok(())
}

/// Runs `bash <script> <args>` in `cwd`, streaming both streams line by line; returns
/// whether it exited with success and the JSON summary from the last line of stdout, if
/// there is one.
pub async fn run_script(script: &Path, args: &[OsString], cwd: &Path, log: &dyn JobLog) -> Result<(bool, Option<Value>)> {
    let name = script.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_else(|| "the script".into());
    let mut cmd = prereqs::command("bash");
    cmd.arg(script).args(args);
    let mut child = cmd
        .current_dir(cwd)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .kill_on_drop(true)
        .spawn()
        .with_context(|| format!("could not start bash {}", script.display()))?;

    let stdout = child.stdout.take().context("no stdout")?;
    let stderr = child.stderr.take().context("no stderr")?;
    let (tx, mut rx) = tokio::sync::mpsc::unbounded_channel::<(&'static str, String)>();

    let tx_out = tx.clone();
    let out_task = tokio::spawn(async move {
        let mut lines = BufReader::new(stdout).lines();
        while let Ok(Some(line)) = lines.next_line().await {
            let _ = tx_out.send(("stdout", line));
        }
    });
    let err_task = tokio::spawn(async move {
        let mut lines = BufReader::new(stderr).lines();
        while let Ok(Some(line)) = lines.next_line().await {
            let _ = tx.send(("stderr", line));
        }
    });

    let mut last_stdout: Option<String> = None;
    while let Some((stream, line)) = rx.recv().await {
        if stream == "stdout" && !line.trim().is_empty() {
            last_stdout = Some(line.clone());
        }
        log.push(stream, &line);
    }
    let _ = out_task.await;
    let _ = err_task.await;
    let status = child.wait().await.with_context(|| format!("{name} did not finish"))?;
    if !status.success() {
        log.app(&format!("{name} exited with {}", status.code().map(|c| c.to_string()).unwrap_or_else(|| "a signal".into())));
    }

    let summary = last_stdout
        .as_deref()
        .map(str::trim)
        .filter(|l| l.starts_with('{'))
        .and_then(|l| serde_json::from_str::<Value>(l).ok());
    if summary.is_none() {
        log.app("No JSON summary line at the end of the output.");
    }
    Ok((status.success(), summary))
}

/// The plugins a build put in place, from the JSON line `build.sh` ends with. A MIDI tool
/// adds `componentFx`, its AU MIDI effect; an empty path counts as none.
#[derive(Debug, Default, PartialEq, Eq)]
struct Installed {
    component: Option<String>,
    component_fx: Option<String>,
    vst3: Option<String>,
}

impl Installed {
    fn from_summary(s: &Value) -> Installed {
        let path = |key: &str| str_field(s, &[key]).filter(|p| !p.is_empty());
        Installed { component: path("component"), component_fx: path("componentFx"), vst3: path("vst3") }
    }
}

fn str_field(v: &Value, keys: &[&str]) -> Option<String> {
    keys.iter().find_map(|k| v.get(*k)).and_then(|x| match x {
        Value::String(s) => Some(s.clone()),
        Value::Number(n) => Some(n.to_string()),
        _ => None,
    })
}

/// e.g. "arm64, macOS 26.0.1"
async fn machine_string() -> String {
    let arch = match std::env::consts::ARCH {
        "aarch64" => "arm64".to_string(),
        other => other.to_string(),
    };
    let ver = prereqs::command("sw_vers")
        .arg("-productVersion")
        .output()
        .await
        .ok()
        .map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string())
        .filter(|s| !s.is_empty())
        .unwrap_or_else(|| "unknown".into());
    format!("{arch}, macOS {ver}")
}

async fn faust_version_from_tool() -> Option<String> {
    let out = prereqs::command("faust").arg("--version").output().await.ok()?;
    prereqs::parse_faust_version(&String::from_utf8_lossy(&out.stdout))
}

/// `<shell>/VERSION` if the shell keeps one, else the repository's git short hash, else none.
async fn shell_version_from_dir(shell_dir: &str) -> Option<String> {
    let dir = PathBuf::from(shell_dir);
    if let Ok(v) = std::fs::read_to_string(dir.join("VERSION")) {
        let v = v.trim();
        if !v.is_empty() {
            return Some(v.to_string());
        }
    }
    let out = prereqs::command("git")
        .args(["-C", shell_dir, "rev-parse", "--short", "HEAD"])
        .output()
        .await
        .ok()?;
    if out.status.success() {
        let s = String::from_utf8_lossy(&out.stdout).trim().to_string();
        if !s.is_empty() {
            return Some(format!("git {s}"));
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_midi_tool_says_where_both_plugins_go() {
        let m = installed_message("midi", "MIDI Steps");
        assert!(m.contains("MIDI Steps FX in a MIDI FX slot"), "{m}");
        assert!(m.contains("put MIDI Steps on a MIDI track"), "{m}");
        assert_eq!(installed_message("effect", "Tape Drift"), INSTALLED_MESSAGE);
        assert_eq!(installed_message("instrument", "Poly Saw"), INSTALLED_MESSAGE);
        for text in [m.as_str(), MIDI_TOOL_POLICY_MESSAGE] {
            assert!(!text.contains('\u{2014}') && !text.contains('\u{2013}') && !text.contains('!'), "copy rules: {text}");
        }
    }

    /// The line `shell/build.sh shell/examples/midi-steps --install --verify --allow-logic`
    /// printed on 7 November 2026, and an effect's, which has no FX component.
    #[test]
    fn both_plugins_of_a_midi_tool_are_read_from_the_summary() {
        let midi: Value = serde_json::from_str(r#"{"ok": true, "stage": "done", "name": "MIDI Steps", "slug": "midi-steps", "kind": "midi", "version": "0.1.0", "pluginCode": "Mstp", "manufacturerCode": "Pnga", "bundleId": "com.pangea.midi-steps", "logic": true, "component": "/Users/me/Library/Audio/Plug-Ins/Components/MIDI Steps.component", "vst3": "/Users/me/Library/Audio/Plug-Ins/VST3/MIDI Steps.vst3", "installed": true, "auval": "passed", "auvalLog": "/x/shell/build/auval.log", "verify": "passed", "faust": "2.88.0", "faustExpected": "2.88.0", "juce": "8.0.10", "firstBuild": false, "seconds": 15, "fxPluginCode": "Mstx", "componentFx": "/Users/me/Library/Audio/Plug-Ins/Components/MIDI Steps FX.component", "auvalFx": "passed", "auvalFxLog": "/x/shell/build/auval-fx.log"}"#).unwrap();
        let got = Installed::from_summary(&midi);
        assert_eq!(got.component.as_deref(), Some("/Users/me/Library/Audio/Plug-Ins/Components/MIDI Steps.component"));
        assert_eq!(got.component_fx.as_deref(), Some("/Users/me/Library/Audio/Plug-Ins/Components/MIDI Steps FX.component"));
        assert_eq!(got.vst3.as_deref(), Some("/Users/me/Library/Audio/Plug-Ins/VST3/MIDI Steps.vst3"));
        let effect: Value = serde_json::from_str(r#"{"ok": true, "kind": "effect", "component": "/c/Tape Drift.component", "vst3": "/v/Tape Drift.vst3", "auval": "passed"}"#).unwrap();
        assert_eq!(Installed::from_summary(&effect).component_fx, None);
        let failed: Value = serde_json::from_str(r#"{"ok": false, "kind": "midi", "component": "", "vst3": "", "componentFx": ""}"#).unwrap();
        assert_eq!(Installed::from_summary(&failed), Installed::default());
    }

    #[test]
    fn logic_and_midi_tools_build_only_for_their_owner() {
        assert_eq!(policy_refusal("effect", false, false), None);
        assert_eq!(policy_refusal("instrument", true, true), None);
        assert_eq!(policy_refusal("instrument", true, false), Some(LOGIC_POLICY_MESSAGE));
        assert_eq!(policy_refusal("midi", true, true), None);
        assert_eq!(policy_refusal("midi", true, false), Some(MIDI_TOOL_POLICY_MESSAGE));
        // A MIDI tool whose logic went missing is still not built from someone else.
        assert_eq!(policy_refusal("midi", false, false), Some(MIDI_TOOL_POLICY_MESSAGE));
    }
}
