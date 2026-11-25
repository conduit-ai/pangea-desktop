//! The app's side of the Pangea plugin (docs/pangea-plugin.md, "The folder" and "What the app
//! builds"). The plugin runs inside the DAW and shares one folder with the app,
//! `~/Library/Application Support/Pangea/host/`, and nothing else:
//!
//! - `library.json`: the member's plugins, written by the app.
//! - `requests/<requestId>.json`: written by the plugin, read and deleted by the app.
//! - `responses/<requestId>.json`: written by the app, read and deleted by the plugin.
//! - `modules/<projectId>/v<N>/`: a version compiled as a loadable module by `build-module.sh`.
//!
//! Every file is written to `<name>.tmp` and renamed into place. The service handles one
//! request at a time, oldest first. A module is checked exactly as an install is (the shared
//! steps live in `build.rs`); its log goes to stderr, and it is not reported to the web as a
//! build row.
//!
//! What the Pangea plugin has is reported to the site (decision 87): each module the service
//! builds or finds ready, and once after the app signs in the newest module of each project on
//! disk. The site's build view reads it for its main button, Add to Pangea plugin or Update in
//! Pangea plugin, whose link (`pangea://add`, deep_link.rs) the service carries out like a
//! request from the plugin.

use std::ffi::OsString;
use std::future::Future;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant, SystemTime};

use anyhow::{Context, Result};
use serde::Serialize;
use serde_json::Value;
use tauri::{AppHandle, Emitter, Manager, Runtime};

use crate::api::{self, Api, Me, ModuleReport, Project, VersionBundle};
use crate::build::{self, JobLog};
use crate::deep_link::AddRequest;
use crate::{auth, settings, AppState};

/// The version of every file in the folder.
pub const FORMAT: u32 = 1;
pub const NOT_MINE_MESSAGE: &str =
    "For now the Pangea plugin opens your own plugins. Install this one from the Pangea app.";
const REFRESH_FAILED: &str = "The Pangea app could not refresh this list. It shows the last list it had.";
const SESSION_EXPIRED: &str = "Your session has expired. Sign in to the Pangea app again.";
const SIGN_IN_FIRST: &str = "Sign in to the Pangea app first.";
const SCAN_EVERY: Duration = Duration::from_millis(400);
const LIBRARY_EVERY: Duration = Duration::from_secs(20);
/// A request is a few hundred bytes; anything far larger is not one.
const REQUEST_MAX_BYTES: u64 = 64 * 1024;

// ---- The folder ---------------------------------------------------------------------

/// `$HOME/Library/Application Support/Pangea/host`, beside the `projects` folder installs
/// use. Only the service calls this; everything else takes the folder as an argument.
pub fn host_dir() -> Result<PathBuf> {
    Ok(settings::projects_dir()?.with_file_name("host"))
}

pub fn requests_dir(base: &Path) -> PathBuf {
    base.join("requests")
}

pub fn responses_dir(base: &Path) -> PathBuf {
    base.join("responses")
}

/// `<base>/modules/<projectId>/v<N>`. `project_id` must have passed `api::validate_project_id`.
pub fn module_dir(base: &Path, project_id: &str, version: u32) -> PathBuf {
    base.join("modules").join(project_id).join(format!("v{version}"))
}

/// The modules on this Mac for the Pangea plugin: for each folder under `modules/` named by a
/// project id, its newest `v<N>` with a `manifest.json`. Sorted by project id.
pub fn modules_on_disk(base: &Path) -> Vec<ModuleReport> {
    let Ok(projects) = std::fs::read_dir(base.join("modules")) else {
        return Vec::new();
    };
    let mut found = Vec::new();
    for entry in projects.flatten() {
        let Some(project_id) = entry.file_name().to_str().map(str::to_string) else {
            continue;
        };
        if api::validate_project_id(&project_id).is_err() {
            continue;
        }
        let Ok(versions) = std::fs::read_dir(entry.path()) else {
            continue;
        };
        let newest = versions
            .flatten()
            .filter_map(|v| {
                let n: u32 = v.file_name().to_str()?.strip_prefix('v')?.parse().ok()?;
                (n > 0 && v.path().join("manifest.json").is_file()).then_some(n)
            })
            .max();
        if let Some(version) = newest {
            found.push(ModuleReport { project_id, version });
        }
    }
    found.sort_by(|a, b| a.project_id.cmp(&b.project_id));
    found
}

pub fn ensure_dirs(base: &Path) -> Result<()> {
    for dir in [requests_dir(base), responses_dir(base), base.join("modules")] {
        std::fs::create_dir_all(&dir).with_context(|| format!("could not create {}", dir.display()))?;
    }
    Ok(())
}

/// Writes `<name>.tmp` in the same folder, then renames it into place, so a reader sees the
/// old file or the new one and never half of one.
pub fn write_atomic(path: &Path, bytes: &[u8]) -> Result<()> {
    let dir = path.parent().with_context(|| format!("{} has no folder", path.display()))?;
    std::fs::create_dir_all(dir).with_context(|| format!("could not create {}", dir.display()))?;
    let mut tmp_name = path.file_name().with_context(|| format!("{} has no file name", path.display()))?.to_os_string();
    tmp_name.push(".tmp");
    let tmp = dir.join(tmp_name);
    std::fs::write(&tmp, bytes).with_context(|| format!("could not write {}", tmp.display()))?;
    if let Err(e) = std::fs::rename(&tmp, path) {
        let _ = std::fs::remove_file(&tmp);
        return Err(e).with_context(|| format!("could not move {} into place", tmp.display()));
    }
    Ok(())
}

fn write_json<T: Serialize>(path: &Path, value: &T) -> Result<()> {
    let mut text = serde_json::to_string_pretty(value)?;
    text.push('\n');
    write_atomic(path, text.as_bytes())
}

fn utc_now() -> String {
    chrono::Utc::now().to_rfc3339_opts(chrono::SecondsFormat::Secs, true)
}

/// Lines from the service itself. The plugin's jobs have no page to stream to.
fn note(line: &str) {
    eprintln!("[pangea plugin] {line}");
}

/// A request's log, to stderr, each line marked with the request.
struct StderrLog {
    request_id: String,
}

impl JobLog for StderrLog {
    fn push(&self, stream: &str, line: &str) {
        eprintln!("[pangea plugin {}] {stream}: {line}", self.request_id);
    }
}

// ---- library.json -------------------------------------------------------------------

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct Library {
    pub format: u32,
    /// RFC 3339, UTC, to the second.
    pub updated_at: String,
    pub app_pid: u32,
    pub signed_in: bool,
    pub member: Option<Member>,
    /// Why the list could not be refreshed; `plugins` is then the last good list.
    pub error: Option<String>,
    pub plugins: Vec<LibraryPlugin>,
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
pub struct Member {
    /// The member's name, or their email when the account has no name.
    pub name: String,
    pub email: String,
}

impl Member {
    fn of(me: &Me) -> Member {
        let name = me.name.clone().filter(|n| !n.trim().is_empty()).unwrap_or_else(|| me.email.clone());
        Member { name, email: me.email.clone() }
    }
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct LibraryPlugin {
    pub project_id: String,
    pub name: String,
    pub slug: String,
    /// `effect`, `instrument` or `midi`.
    pub kind: String,
    /// `None` for a project with no version yet.
    pub head_version: Option<u32>,
    pub mine: bool,
    /// The owner's name, or whatever the list knows of them.
    pub owner: String,
    pub description: Option<String>,
    pub updated_at: Option<String>,
    /// Where the plugin is filed (decision 85), for the plugin's search and its filter: the
    /// category's id and name, the tags, the other words it is found by as its category (searched, never shown)
    /// and the head version's own summary. `null`, `null`, `[]`, `[]` and `null` when it is
    /// not filed. Added after the fields above, which keep their order.
    pub category: Option<String>,
    pub category_name: Option<String>,
    pub tags: Vec<String>,
    pub keywords: Vec<String>,
    pub summary: Option<String>,
}

impl LibraryPlugin {
    pub fn of(p: &Project, me: &Me) -> LibraryPlugin {
        LibraryPlugin {
            project_id: p.id.clone(),
            name: p.name.clone(),
            slug: p.slug.clone(),
            kind: p.kind.clone(),
            head_version: p.head_version,
            mine: p.owner_details.is(me),
            owner: p.owner_details.name.clone().filter(|n| !n.trim().is_empty()).unwrap_or_else(|| p.owner.clone()),
            description: p.description.clone(),
            updated_at: p.updated_at.clone(),
            category: p.category.clone(),
            category_name: p.category_name.clone(),
            tags: p.tags.clone(),
            keywords: p.keywords.clone(),
            summary: p.summary.clone(),
        }
    }
}

impl Library {
    fn new(signed_in: bool, member: Option<Member>, error: Option<String>, plugins: Vec<LibraryPlugin>) -> Library {
        Library { format: FORMAT, updated_at: utc_now(), app_pid: std::process::id(), signed_in, member, error, plugins }
    }
}

/// The last list fetched, so a failed refresh still shows it. Cleared on sign-out.
#[derive(Debug, Default)]
pub struct LibraryMemory {
    member: Option<Member>,
    plugins: Vec<LibraryPlugin>,
}

/// The list as it should be written now. `api` is `None` when the app is signed out. A failed
/// refresh keeps the last good plugins and says why in `error`; an expired session counts as
/// signed out.
pub async fn refresh_library<A: HostApi>(api: Option<&A>, memory: &mut LibraryMemory) -> Library {
    let Some(api) = api else {
        *memory = LibraryMemory::default();
        return Library::new(false, None, None, Vec::new());
    };
    let fetched = match api.me().await {
        Ok(me) => api.projects().await.map(|projects| (me, projects)),
        Err(e) => Err(e),
    };
    match fetched {
        Ok((me, projects)) => {
            memory.member = Some(Member::of(&me));
            memory.plugins = projects.iter().map(|p| LibraryPlugin::of(p, &me)).collect();
            Library::new(true, memory.member.clone(), None, memory.plugins.clone())
        }
        Err(e) if api::is_unauthorised(&e) => {
            *memory = LibraryMemory::default();
            Library::new(false, None, Some(SESSION_EXPIRED.into()), Vec::new())
        }
        Err(e) => {
            note(&format!("could not refresh the list: {e:#}"));
            Library::new(true, memory.member.clone(), Some(REFRESH_FAILED.into()), memory.plugins.clone())
        }
    }
}

pub fn write_library(base: &Path, library: &Library) -> Result<()> {
    write_json(&base.join("library.json"), library)
}

// ---- Requests -----------------------------------------------------------------------

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Request {
    /// Write `library.json` now, then answer.
    Library,
    /// Build (or find) a version as a module. `version: None` is the head version, looked up fresh.
    Module { project_id: String, version: Option<u32>, rebuild: bool },
}

/// What a request file turned out to hold.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Taken {
    Request(Request),
    /// A request that can be answered but not carried out: the answer is `failed` with this message.
    Invalid(String),
    /// Not a JSON object: no answer is written.
    Unreadable,
}

/// 8 to 64 letters, digits and hyphens.
pub fn valid_request_id(id: &str) -> bool {
    (8..=64).contains(&id.len()) && id.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'-')
}

/// The requests waiting in `requests/`, as `(requestId, path)`, oldest first by modification
/// time (then by id). A `*.json` file whose name is not a valid request id is deleted on the
/// way. Anything else, such as a `.tmp` file still being written, is left alone.
pub fn pending_requests(base: &Path) -> Vec<(String, PathBuf)> {
    let Ok(entries) = std::fs::read_dir(requests_dir(base)) else {
        return Vec::new();
    };
    let mut found = Vec::new();
    for entry in entries.flatten() {
        let path = entry.path();
        let name = entry.file_name().to_string_lossy().to_string();
        let Some(id) = name.strip_suffix(".json") else { continue };
        let Ok(meta) = entry.metadata() else { continue };
        if !meta.is_file() {
            continue;
        }
        if !valid_request_id(id) {
            note(&format!("deleted {name}: its name is not a request id"));
            let _ = std::fs::remove_file(&path);
            continue;
        }
        found.push((meta.modified().unwrap_or(SystemTime::UNIX_EPOCH), id.to_string(), path));
    }
    found.sort();
    found.into_iter().map(|(_, id, path)| (id, path)).collect()
}

/// Reads a request file and deletes it before anything is done, so a request is carried out
/// at most once even if the app stops halfway through it.
pub fn take_request(path: &Path, id: &str) -> Taken {
    let text = match std::fs::metadata(path) {
        Ok(m) if m.len() <= REQUEST_MAX_BYTES => std::fs::read_to_string(path).ok(),
        _ => None,
    };
    let _ = std::fs::remove_file(path);
    match text {
        Some(text) => parse_request(id, &text),
        None => Taken::Unreadable,
    }
}

/// Parses a request whose file name gave `file_id`. A missing `format` or `requestId` is
/// taken as 1 and the file name.
pub fn parse_request(file_id: &str, text: &str) -> Taken {
    let Ok(Value::Object(v)) = serde_json::from_str::<Value>(text) else {
        return Taken::Unreadable;
    };
    let invalid = |m: &str| Taken::Invalid(m.to_string());
    match v.get("format") {
        None => {}
        Some(f) if f.as_u64() == Some(u64::from(FORMAT)) => {}
        Some(_) => return invalid("This request is in a format this Pangea app does not read. Update the Pangea app."),
    }
    match v.get("requestId") {
        None => {}
        Some(Value::String(s)) if s == file_id => {}
        Some(_) => return invalid("The request's id does not match its file name."),
    }
    match v.get("type").and_then(Value::as_str) {
        Some("library") => Taken::Request(Request::Library),
        Some("module") => {
            let project_id = match v.get("projectId").and_then(Value::as_str) {
                Some(p) if api::validate_project_id(p).is_ok() => p.to_string(),
                _ => return invalid("The request has no valid project id."),
            };
            let version = match v.get("version") {
                None | Some(Value::Null) => None,
                Some(n) => match n.as_u64().and_then(|n| u32::try_from(n).ok()) {
                    Some(n) if api::validate_version(n).is_ok() => Some(n),
                    _ => return invalid("The request's version is not a version number."),
                },
            };
            let rebuild = match v.get("rebuild") {
                None | Some(Value::Null) => false,
                Some(Value::Bool(b)) => *b,
                Some(_) => return invalid("The request's rebuild must be true or false."),
            };
            Taken::Request(Request::Module { project_id, version, rebuild })
        }
        _ => invalid("This Pangea app does not know the request's type."),
    }
}

// ---- Responses ----------------------------------------------------------------------

#[derive(Debug, Clone, Copy, Serialize, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum Outcome {
    /// The module is on disk, or `library.json` was written.
    Ready,
    /// The checks or the policy said no: nothing was built.
    Refused,
    /// Anything else that stopped the request, the build included.
    Failed,
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct Response {
    pub format: u32,
    pub request_id: String,
    /// True exactly when `outcome` is `ready`.
    pub ok: bool,
    pub outcome: Outcome,
    pub project_id: Option<String>,
    pub version: Option<u32>,
    /// Absolute, and only with `ready` for a module.
    pub module_dir: Option<String>,
    pub message: String,
}

impl Response {
    pub fn new(
        request_id: &str,
        outcome: Outcome,
        project_id: Option<&str>,
        version: Option<u32>,
        module_dir: Option<&Path>,
        message: impl Into<String>,
    ) -> Response {
        Response {
            format: FORMAT,
            request_id: request_id.to_string(),
            ok: outcome == Outcome::Ready,
            outcome,
            project_id: project_id.map(str::to_string),
            version,
            module_dir: module_dir.map(|d| d.display().to_string()),
            message: message.into(),
        }
    }
}

pub fn write_response(base: &Path, response: &Response) -> Result<()> {
    write_json(&responses_dir(base).join(format!("{}.json", response.request_id)), response)
}

// ---- Carrying out a request ---------------------------------------------------------

/// The calls a request makes to the Pangea web API, behind a trait so tests can fake them.
pub trait HostApi {
    fn me(&self) -> impl Future<Output = Result<Me>> + Send;
    fn projects(&self) -> impl Future<Output = Result<Vec<Project>>> + Send;
    fn version(&self, project_id: &str, number: u32) -> impl Future<Output = Result<VersionBundle>> + Send;
}

impl HostApi for Api {
    async fn me(&self) -> Result<Me> {
        Api::me(self).await
    }
    async fn projects(&self) -> Result<Vec<Project>> {
        Api::projects(self).await
    }
    async fn version(&self, project_id: &str, number: u32) -> Result<VersionBundle> {
        Api::version(self, project_id, number).await
    }
}

/// Where a request reads and writes. The service fills it from `host_dir()` and the shell builds
/// use (`settings::shell_dir`: the app's own, or a developer's folder); tests use temporary folders.
pub struct HostEnv {
    /// The shared folder (`host/`).
    pub base: PathBuf,
    /// The folder that holds `build-module.sh` and `check-project.sh`.
    pub shell_dir: String,
}

/// Where a version's sources are written before its module is built: `<base>/sources/<projectId>/v<N>`.
/// Only this service writes there, one request at a time, and the folder is named by project id,
/// so no install and no other project (which may share a slug, and may carry someone else's
/// logic module) can change the files between the checks and the compiler.
pub fn sources_dir(base: &Path, project_id: &str, version: u32) -> PathBuf {
    base.join("sources").join(project_id).join(format!("v{version}"))
}

/// Carries out one request and returns the answer to write. `api` is `None` when the app is
/// signed out. A `library` request writes `library.json` before it is answered.
pub async fn handle_request<A: HostApi>(
    api: Option<&A>,
    env: &HostEnv,
    memory: &mut LibraryMemory,
    request_id: &str,
    request: &Request,
    log: &dyn JobLog,
) -> Response {
    match request {
        Request::Library => {
            let library = refresh_library(api, memory).await;
            match write_library(&env.base, &library) {
                Ok(()) => {
                    let message = match (&library.error, library.signed_in) {
                        (Some(e), _) => e.clone(),
                        (None, false) => "Sign in to the Pangea app to see your plugins.".to_string(),
                        (None, true) => "The list is up to date.".to_string(),
                    };
                    Response::new(request_id, Outcome::Ready, None, None, None, message)
                }
                Err(e) => {
                    log.app(&format!("could not write library.json: {e:#}"));
                    Response::new(request_id, Outcome::Failed, None, None, None, "The Pangea app could not write the list.")
                }
            }
        }
        Request::Module { project_id, version, rebuild } => match api {
            None => Response::new(request_id, Outcome::Failed, Some(project_id), *version, None, SIGN_IN_FIRST),
            Some(api) => build_module(api, env, request_id, project_id, *version, *rebuild, log).await,
        },
    }
}

/// The answer to one module request, filled in as the version becomes known.
struct Reply<'a> {
    request_id: &'a str,
    project_id: &'a str,
    version: Option<u32>,
}

impl Reply<'_> {
    fn answer(&self, outcome: Outcome, module_dir: Option<&Path>, message: impl Into<String>) -> Response {
        Response::new(self.request_id, outcome, Some(self.project_id), self.version, module_dir, message)
    }
    fn ready(&self, module_dir: &Path, message: impl Into<String>) -> Response {
        self.answer(Outcome::Ready, Some(module_dir), message)
    }
    fn refused(&self, message: impl Into<String>) -> Response {
        self.answer(Outcome::Refused, None, message)
    }
    fn failed(&self, message: impl Into<String>) -> Response {
        self.answer(Outcome::Failed, None, message)
    }
}

/// A plain sentence for an API error; the details go to the log.
fn api_problem(e: &anyhow::Error, plain: &str, log: &dyn JobLog) -> String {
    log.push("stderr", &format!("{e:#}"));
    if api::is_unauthorised(e) {
        SESSION_EXPIRED.to_string()
    } else {
        plain.to_string()
    }
}

/// A module request, from the doc's "What the app builds": the member's own projects only;
/// a module with a `manifest.json` is ready at once unless `rebuild`; otherwise the version
/// is fetched, checked in memory, written to the projects folder, checked on disk and
/// compiled by `build-module.sh`.
pub async fn build_module<A: HostApi>(
    api: &A,
    env: &HostEnv,
    request_id: &str,
    project_id: &str,
    version: Option<u32>,
    rebuild: bool,
    log: &dyn JobLog,
) -> Response {
    let mut reply = Reply { request_id, project_id, version };

    // 1. Whose project it is, and which version
    let me = match api.me().await {
        Ok(me) => me,
        Err(e) => return reply.failed(api_problem(&e, "The Pangea app could not reach Pangea.", log)),
    };
    let projects = match api.projects().await {
        Ok(p) => p,
        Err(e) => return reply.failed(api_problem(&e, "The Pangea app could not get your plugins from Pangea.", log)),
    };
    let Some(listed) = projects.into_iter().find(|p| p.id == project_id) else {
        return reply.failed("This plugin is not in your Pangea list.");
    };
    if !listed.owner_details.is(&me) {
        log.app(&format!("Refused {}: it is not the member's own", listed.name));
        return reply.refused(NOT_MINE_MESSAGE);
    }
    let Some(number) = version.or(listed.head_version) else {
        return reply.failed(format!("{} has no version yet.", listed.name));
    };
    reply.version = Some(number);
    let module = module_dir(&env.base, project_id, number);
    let manifest = module.join("manifest.json");
    let ready_message = format!("{} v{number} is ready.", listed.name);
    if !rebuild && manifest.is_file() {
        log.app(&format!("{} is already built", module.display()));
        return reply.ready(&module, ready_message);
    }
    let script = Path::new(&env.shell_dir).join("build-module.sh");
    if env.shell_dir.trim().is_empty() || !script.is_file() {
        return reply.failed(format!(
            "The shell has no build-module.sh ({}). Open the Pangea app: its Setup says what is missing.",
            if env.shell_dir.trim().is_empty() { "the app has no shell" } else { env.shell_dir.as_str() }
        ));
    }

    // 2. Fetch the version. The version's own owner, when it names one, must agree.
    log.app(&format!("Fetching {} v{number}", listed.name));
    let bundle = match api.version(project_id, number).await {
        Ok(b) => b,
        Err(e) => {
            let plain = format!("The Pangea app could not fetch {} v{number}.", listed.name);
            return reply.failed(api_problem(&e, &plain, log));
        }
    };
    if !bundle.owner.is_empty() && !bundle.owner.is(&me) {
        log.app(&format!("Refused {}: the version names another owner", listed.name));
        return reply.refused(NOT_MINE_MESSAGE);
    }
    let kind = bundle.plugin.get("kind").and_then(Value::as_str).unwrap_or(&listed.kind).to_string();

    // 3. Check the sources and the interface in memory, before anything touches the disk
    log.app("Checking the sources before writing them");
    let (found, ui_text) = build::check_in_memory(&bundle);
    if !found.is_empty() {
        for v in &found {
            log.push("stderr", &v.to_string());
        }
        log.app("Refused: nothing was written or built.");
        return reply.refused(format!("Not built: {}", build::refusal_reason(&found)));
    }

    // 4. Write the sources to this service's own folder for the project (see `sources_dir`)
    let dir = sources_dir(&env.base, project_id, number);
    if let Err(e) = build::write_project(&dir, &bundle, ui_text.as_deref()) {
        log.push("stderr", &format!("{e:#}"));
        return reply.failed(format!("The Pangea app could not write {} v{number} to disk.", listed.name));
    }
    log.app(&format!("Wrote {}", dir.display()));

    // 5. The policy, then the checks on disk (the app's checker and check-project.sh). Every
    //    project built here is the member's own, so the policy is there for when that changes.
    let mine = true;
    let refusal = match build::policy_refusal(&kind, bundle.logic.is_some(), mine) {
        Some(p) => {
            log.push("stderr", p);
            Some(p.to_string())
        }
        None => build::safety_check(&env.shell_dir, &dir, log).await,
    };
    if let Some(reason) = refusal {
        build::remove_refused(&dir, log);
        log.app("Refused: nothing was built.");
        return reply.refused(format!("Not built: {reason}"));
    }

    // 6. Build. build-module.sh builds beside the module folder and swaps it in at the end,
    //    so a failed build keeps the old folder: its manifest goes first, so the next request
    //    does not call a module ready that the plugin asked to have rebuilt. Only the
    //    project's folder is created here; the module folder is the script's to put in place.
    let modules_of_project = module.parent().unwrap_or(&env.base);
    if let Err(e) = remove_if_present(&manifest).and_then(|()| std::fs::create_dir_all(modules_of_project)) {
        log.push("stderr", &format!("{}: {e}", module.display()));
        return reply.failed("The Pangea app could not prepare the module folder.");
    }
    let allow_logic = mine && bundle.logic.is_some();
    let number_text = number.to_string();
    let mut args: Vec<OsString> = vec![
        dir.clone().into_os_string(),
        module.clone().into_os_string(),
        "--project-id".into(),
        project_id.into(),
        "--version-number".into(),
        number_text.clone().into(),
    ];
    if allow_logic {
        args.push("--allow-logic".into());
    }
    log.app(&format!(
        "$ bash {} {} {} --project-id {project_id} --version-number {number_text}{}",
        script.display(),
        dir.display(),
        module.display(),
        if allow_logic { " --allow-logic" } else { "" }
    ));
    let (status_ok, summary) = match build::run_script(&script, &args, &dir, log).await {
        Ok(r) => r,
        Err(e) => {
            log.push("stderr", &format!("{e:#}"));
            return reply.failed("The Pangea app could not run build-module.sh.");
        }
    };

    // 7. Judge: the exit status, the summary line and the manifest must all agree.
    let summary_ok = summary.as_ref().and_then(|s| s.get("ok")).and_then(Value::as_bool).unwrap_or(true);
    if !(status_ok && summary_ok) {
        let error = summary
            .as_ref()
            .and_then(|s| s.get("error"))
            .and_then(Value::as_str)
            .filter(|e| !e.trim().is_empty())
            .map(str::to_string)
            .unwrap_or_else(|| "build-module.sh stopped without saying why.".to_string());
        log.app("Failed.");
        return reply.failed(error);
    }
    if !manifest.is_file() {
        log.app("Failed: no manifest.json.");
        return reply.failed("build-module.sh finished but wrote no manifest.json.");
    }
    log.app("Done.");
    reply.ready(&module, ready_message)
}

fn remove_if_present(path: &Path) -> std::io::Result<()> {
    match std::fs::remove_file(path) {
        Err(e) if e.kind() != std::io::ErrorKind::NotFound => Err(e),
        _ => Ok(()),
    }
}

// ---- The service --------------------------------------------------------------------

/// Starts the service for as long as the app runs: it keeps `library.json` up to date and
/// answers the plugin's requests one at a time.
pub fn start<R: Runtime>(app: &AppHandle<R>) {
    let app = app.clone();
    tauri::async_runtime::spawn(async move {
        let base = match host_dir().and_then(|b| ensure_dirs(&b).map(|()| b)) {
            Ok(b) => b,
            Err(e) => {
                note(&format!("not started: {e:#}"));
                return;
            }
        };
        let mut service = Service {
            base,
            memory: LibraryMemory::default(),
            session: None,
            last_refresh: None,
            retry_session_at: None,
            reported_for: None,
        };
        loop {
            service.turn(&app).await;
        }
    });
}

struct Service {
    base: PathBuf,
    memory: LibraryMemory,
    /// The API base URL and token fingerprint last seen (`auth::session`); `None` before the first look.
    session: Option<(String, Option<u64>)>,
    last_refresh: Option<Instant>,
    /// After the settings or the keychain could not be read, when to look again.
    retry_session_at: Option<Instant>,
    /// The session whose modules on disk were last reported to the site (decision 87).
    reported_for: Option<(String, Option<u64>)>,
}

/// What the app's window is told after an add link (decision 87), to show it.
pub const ADDED_EVENT: &str = "host-added";

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Added {
    pub ok: bool,
    pub message: String,
}

/// The oldest add link waiting, if any.
fn take_pending_add<R: Runtime>(app: &AppHandle<R>) -> Option<AddRequest> {
    let state = app.state::<AppState>();
    let mut pending = state.pending_adds.lock().ok()?;
    if pending.is_empty() {
        None
    } else {
        Some(pending.remove(0))
    }
}

/// Tells the site the Pangea plugin has a project's version. A failure is noted and changes
/// nothing on this Mac: the next module of the project reports it again.
async fn report_module(api: &Api, project_id: &str, version: u32) {
    let module = [ModuleReport { project_id: project_id.to_string(), version }];
    if let Err(e) = api.report_modules(&module).await {
        note(&format!("could not tell Pangea the plugin has {project_id} v{version}: {e:#}"));
    }
}

impl Service {
    /// One turn: write `library.json` when the member signed in or out (or on the first turn)
    /// and every 20 seconds while signed in; then answer the oldest request, or wait 400 ms.
    async fn turn<R: Runtime>(&mut self, app: &AppHandle<R>) {
        let session = self.look_at_session(app);
        let changed = self.session.as_ref() != Some(&session);
        let due = session.1.is_some() && self.last_refresh.is_none_or(|t| t.elapsed() >= LIBRARY_EVERY);
        self.session = Some(session.clone());
        if changed || due {
            self.refresh(app).await;
        }
        // Once a session, what is already on this Mac (decision 87).
        if session.1.is_some() && self.reported_for.as_ref() != Some(&session) {
            self.reported_for = Some(session);
            self.report_disk(app).await;
        }
        if let Some(add) = take_pending_add(app) {
            self.add(app, add).await;
            return;
        }
        match pending_requests(&self.base).into_iter().next() {
            Some((id, path)) => self.answer(app, &id, &path).await,
            None => tokio::time::sleep(SCAN_EVERY).await,
        }
    }

    /// `auth::session` reads this launch's token cache, so this is cheap every turn. When the
    /// settings or the keychain cannot be read, the app counts as signed out and looks again
    /// after 20 seconds rather than every turn.
    fn look_at_session<R: Runtime>(&mut self, app: &AppHandle<R>) -> (String, Option<u64>) {
        if let (Some(at), Some(last)) = (self.retry_session_at, &self.session) {
            if Instant::now() < at {
                return last.clone();
            }
        }
        match auth::session(app) {
            Ok(s) => {
                self.retry_session_at = None;
                s
            }
            Err(e) => {
                note(&format!("could not tell whether the app is signed in: {e:#}"));
                self.retry_session_at = Some(Instant::now() + LIBRARY_EVERY);
                (String::new(), None)
            }
        }
    }

    async fn refresh<R: Runtime>(&mut self, app: &AppHandle<R>) {
        let api = auth::signed_in_client(app).ok();
        let library = refresh_library(api.as_ref(), &mut self.memory).await;
        if let Err(e) = write_library(&self.base, &library) {
            note(&format!("could not write library.json: {e:#}"));
        }
        self.last_refresh = Some(Instant::now());
    }

    /// Reports the newest module of each project on this Mac.
    async fn report_disk<R: Runtime>(&self, app: &AppHandle<R>) {
        let Ok(api) = auth::signed_in_client(app) else {
            return;
        };
        let modules = modules_on_disk(&self.base);
        if modules.is_empty() {
            return;
        }
        match api.report_modules(&modules).await {
            Ok(()) => note(&format!("told Pangea the plugin has {} plugins", modules.len())),
            Err(e) => note(&format!("could not tell Pangea what the plugin has: {e:#}")),
        }
    }

    /// An add link (decision 87): the version is built as a module, or found ready, exactly as
    /// for a request from the plugin, then reported, and `library.json` is written at once so
    /// the plugin sees the version without waiting for the next refresh.
    async fn add<R: Runtime>(&mut self, app: &AppHandle<R>, add: AddRequest) {
        let id = format!("site-add-{}", add.version);
        let log = StderrLog { request_id: id.clone() };
        log.app(&format!("Add {} v{}", add.project, add.version));
        let added = match (auth::signed_in_client(app).ok(), settings::load(app)) {
            (None, _) => Added { ok: false, message: SIGN_IN_FIRST.to_string() },
            (Some(_), Err(e)) => {
                log.push("stderr", &format!("{e:#}"));
                Added { ok: false, message: "The Pangea app could not read its settings.".to_string() }
            }
            (Some(api), Ok(cfg)) => {
                let shell_dir = settings::shell_dir(app, &cfg);
                let env = HostEnv { base: self.base.clone(), shell_dir };
                let response =
                    build_module(&api, &env, &id, &add.project, Some(add.version), false, &log).await;
                if response.outcome == Outcome::Ready {
                    report_module(&api, &add.project, add.version).await;
                    self.refresh(app).await;
                    Added {
                        ok: true,
                        message: format!(
                            "{} In your DAW, choose it in Pangea, or press Refresh where it is open.",
                            response.message
                        ),
                    }
                } else {
                    Added { ok: false, message: response.message }
                }
            }
        };
        log.app(&format!("{}: {}", if added.ok { "Ready" } else { "Not done" }, added.message));
        let _ = app.emit(ADDED_EVENT, &added);
    }

    async fn answer<R: Runtime>(&mut self, app: &AppHandle<R>, id: &str, path: &Path) {
        let log = StderrLog { request_id: id.to_string() };
        let mut module_asked = false;
        // A request means the Pangea plugin is running in a DAW: Setup's last tick (setup.rs).
        if let Ok(mut seen) = app.state::<AppState>().plugin_seen.lock() {
            *seen = Some(chrono::Local::now().format("%H:%M").to_string());
        }
        let response = match take_request(path, id) {
            Taken::Unreadable => {
                log.app("ignored and deleted: the file is not a JSON request");
                return;
            }
            Taken::Invalid(message) => Response::new(id, Outcome::Failed, None, None, None, message),
            Taken::Request(request) => {
                log.app(&format!("{request:?}"));
                module_asked = matches!(request, Request::Module { .. });
                let env = settings::load(app).map(|cfg| HostEnv { base: self.base.clone(), shell_dir: settings::shell_dir(app, &cfg) });
                match env {
                    Ok(env) => {
                        let api = auth::signed_in_client(app).ok();
                        let r = handle_request(api.as_ref(), &env, &mut self.memory, id, &request, &log).await;
                        if request == Request::Library {
                            self.last_refresh = Some(Instant::now());
                        }
                        r
                    }
                    Err(e) => {
                        log.push("stderr", &format!("{e:#}"));
                        Response::new(id, Outcome::Failed, None, None, None, "The Pangea app could not read its settings.")
                    }
                }
            }
        };
        log.app(&format!("{:?}: {}", response.outcome, response.message));
        if let Err(e) = write_response(&self.base, &response) {
            log.push("stderr", &format!("could not write the response: {e:#}"));
        }
        // The plugin has the module now: the site's build view should know (decision 87).
        if let (true, Outcome::Ready, Some(project_id), Some(version)) =
            (module_asked, response.outcome, response.project_id.as_deref(), response.version)
        {
            if let Ok(api) = auth::signed_in_client(app) {
                report_module(&api, project_id, version).await;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::api::Owner;
    use std::sync::atomic::{AtomicU32, Ordering};
    use std::sync::Mutex;

    const ID: &str = "3f2a1c4e-9b7d-4e6a-8c1f-0a2b3c4d5e6f";
    const OTHER_ID: &str = "9c8b7a6d-1e2f-4a3b-8c4d-5e6f7a8b9c0d";
    const GOOD_FAUST: &str = "import(\"stdfaust.lib\");\nprocess = _;\n";

    #[test]
    fn modules_on_disk_reports_the_newest_built_version_of_each_project() {
        let dir = TempDir::new("disk");
        let base = &dir.0;
        // v1 and v3 built, v4 begun with no manifest: v3. Another project at v2.
        for (id, v, manifest) in [(ID, 1, true), (ID, 3, true), (ID, 4, false), (OTHER_ID, 2, true)] {
            let m = module_dir(base, id, v);
            std::fs::create_dir_all(&m).unwrap();
            if manifest {
                std::fs::write(m.join("manifest.json"), "{}").unwrap();
            }
        }
        // A folder that is not a project id, and a stray file, are left out.
        let stray = base.join("modules").join("not-a-project").join("v1");
        std::fs::create_dir_all(&stray).unwrap();
        std::fs::write(stray.join("manifest.json"), "{}").unwrap();
        std::fs::write(base.join("modules").join("stray.txt"), "x").unwrap();
        assert_eq!(
            modules_on_disk(base),
            vec![
                ModuleReport { project_id: ID.to_string(), version: 3 },
                ModuleReport { project_id: OTHER_ID.to_string(), version: 2 },
            ]
        );
        assert!(modules_on_disk(&base.join("nowhere")).is_empty());
    }

    /// A folder under the system's temporary folder, removed when dropped.
    struct TempDir(PathBuf);

    impl TempDir {
        fn new(tag: &str) -> TempDir {
            static N: AtomicU32 = AtomicU32::new(0);
            let nanos = SystemTime::now().duration_since(SystemTime::UNIX_EPOCH).map(|d| d.as_nanos()).unwrap_or(0);
            let p = std::env::temp_dir().join(format!(
                "pangea-host-test-{}-{}-{nanos}-{tag}",
                std::process::id(),
                N.fetch_add(1, Ordering::SeqCst)
            ));
            std::fs::create_dir_all(&p).unwrap();
            TempDir(p)
        }
        fn path(&self) -> &Path {
            &self.0
        }
    }

    impl Drop for TempDir {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.0);
        }
    }

    struct QuietLog;
    impl JobLog for QuietLog {
        fn push(&self, _stream: &str, _line: &str) {}
    }

    fn me() -> Me {
        Me { id: "u1".into(), email: "ada@example.com".into(), name: Some("Ada".into()) }
    }

    fn project(id: &str, name: &str, owner: Owner, head: Option<u32>) -> Project {
        let slug = name.to_lowercase().replace(' ', "-");
        Project {
            id: id.into(),
            name: name.into(),
            slug,
            kind: "effect".into(),
            visibility: "private".into(),
            head_version: head,
            owner: owner.display(),
            owner_details: owner,
            description: Some("Warm tape wobble.".into()),
            updated_at: Some("2026-11-10T16:00:00Z".into()),
            category: Some("delay".into()),
            category_name: Some("Delay".into()),
            tags: vec!["tape".into(), "dub".into()],
            keywords: vec!["echo".into()],
            summary: Some("A tape echo that wobbles.".into()),
        }
    }

    fn mine() -> Owner {
        Owner { id: Some("u1".into()), name: Some("Ada".into()), email: Some("ada@example.com".into()) }
    }

    fn someone_else() -> Owner {
        Owner { id: Some("u2".into()), name: Some("Sam".into()), email: Some("sam@example.com".into()) }
    }

    fn bundle(number: u32, logic: Option<&str>) -> VersionBundle {
        VersionBundle {
            version_id: "v-1".into(),
            number,
            slug: "tape-drift".into(),
            plugin: serde_json::json!({ "name": "Tape Drift", "slug": "tape-drift", "kind": "effect" }),
            faust: GOOD_FAUST.into(),
            logic: logic.map(str::to_string),
            ui: Value::Null,
            owner: Owner::default(),
        }
    }

    /// The web API as a test sets it up; counts the version fetches.
    struct Fake {
        projects: Mutex<Result<Vec<Project>, String>>,
        bundle: Option<VersionBundle>,
        fetches: AtomicU32,
    }

    impl Fake {
        fn new(projects: Vec<Project>, bundle: Option<VersionBundle>) -> Fake {
            Fake { projects: Mutex::new(Ok(projects)), bundle, fetches: AtomicU32::new(0) }
        }
    }

    impl HostApi for Fake {
        async fn me(&self) -> Result<Me> {
            Ok(me())
        }
        async fn projects(&self) -> Result<Vec<Project>> {
            self.projects.lock().unwrap().clone().map_err(|e| anyhow::anyhow!(e))
        }
        async fn version(&self, _project_id: &str, _number: u32) -> Result<VersionBundle> {
            self.fetches.fetch_add(1, Ordering::SeqCst);
            self.bundle.clone().ok_or_else(|| anyhow::anyhow!("no such version"))
        }
    }

    fn env(t: &TempDir, shell_dir: &str) -> HostEnv {
        let base = t.path().join("host");
        ensure_dirs(&base).unwrap();
        HostEnv { base, shell_dir: shell_dir.to_string() }
    }

    fn keys(v: &Value) -> Vec<String> {
        v.as_object().unwrap().keys().cloned().collect()
    }

    #[test]
    fn request_ids_are_8_to_64_letters_digits_and_hyphens() {
        assert!(valid_request_id("abcd1234"));
        assert!(valid_request_id("A1-b2-C3-d4"));
        assert!(valid_request_id(&"x".repeat(64)));
        for bad in ["abc123", "", &"x".repeat(65), "abcd_1234", "abcd.1234", "../../etc", "abcd 1234", "abcd1234\u{e9}"] {
            assert!(!valid_request_id(bad), "{bad:?} should be refused");
        }
    }

    #[test]
    fn requests_are_parsed_and_checked() {
        let good = format!(r#"{{ "format": 1, "requestId": "req-0001", "type": "module", "projectId": "{ID}", "version": null, "rebuild": false, "createdAt": "2026-11-10T17:00:00Z" }}"#);
        assert_eq!(
            parse_request("req-0001", &good),
            Taken::Request(Request::Module { project_id: ID.into(), version: None, rebuild: false })
        );
        let pinned = format!(r#"{{ "format": 1, "requestId": "req-0002", "type": "module", "projectId": "{ID}", "version": 7, "rebuild": true }}"#);
        assert_eq!(
            parse_request("req-0002", &pinned),
            Taken::Request(Request::Module { project_id: ID.into(), version: Some(7), rebuild: true })
        );
        let library = r#"{ "format": 1, "requestId": "req-0003", "type": "library", "createdAt": "2026-11-10T17:00:00Z" }"#;
        assert_eq!(parse_request("req-0003", library), Taken::Request(Request::Library));

        // Answered with `failed`: the request is JSON but cannot be carried out
        let invalid = |text: &str| matches!(parse_request("req-0004", text), Taken::Invalid(_));
        assert!(invalid(r#"{ "format": 1, "requestId": "req-0004", "type": "module", "projectId": "../me" }"#));
        assert!(invalid(r#"{ "format": 1, "requestId": "req-0004", "type": "module" }"#));
        assert!(invalid(&format!(r#"{{ "format": 1, "requestId": "req-0004", "type": "module", "projectId": "{ID}", "version": 0 }}"#)));
        assert!(invalid(&format!(r#"{{ "format": 1, "requestId": "req-0004", "type": "module", "projectId": "{ID}", "version": "7" }}"#)));
        assert!(invalid(&format!(r#"{{ "format": 1, "requestId": "req-0004", "type": "module", "projectId": "{ID}", "rebuild": "yes" }}"#)));
        assert!(invalid(r#"{ "format": 1, "requestId": "someone-else", "type": "library" }"#));
        assert!(invalid(r#"{ "format": 2, "requestId": "req-0004", "type": "library" }"#));
        assert!(invalid(r#"{ "format": 1, "requestId": "req-0004", "type": "install" }"#));

        // Not answered at all
        for bad in ["", "{", "not json", "[1, 2]", "\"library\"", "{ \"format\": 1, "] {
            assert_eq!(parse_request("req-0004", bad), Taken::Unreadable, "{bad:?}");
        }
    }

    #[test]
    fn requests_are_taken_oldest_first_and_other_files_are_left_alone() {
        let t = TempDir::new("order");
        let base = t.path();
        ensure_dirs(base).unwrap();
        let dir = requests_dir(base);
        let now = SystemTime::now();
        // Names in the opposite order to their times, so the order cannot come from the names.
        for (name, age) in [("aaaa-0001", 1), ("bbbb-0002", 3), ("cccc-0003", 2)] {
            let p = dir.join(format!("{name}.json"));
            std::fs::write(&p, r#"{ "format": 1, "type": "library" }"#).unwrap();
            let f = std::fs::File::options().write(true).open(&p).unwrap();
            f.set_modified(now - Duration::from_secs(age * 60)).unwrap();
        }
        std::fs::write(dir.join("dddd-0004.json.tmp"), "{").unwrap();
        std::fs::write(dir.join("notes.txt"), "hello").unwrap();
        std::fs::write(dir.join("short.json"), "{}").unwrap();
        std::fs::write(dir.join("bad_name_1.json"), "{}").unwrap();
        std::fs::create_dir_all(dir.join("folder-0005.json")).unwrap();

        let ids: Vec<String> = pending_requests(base).into_iter().map(|(id, _)| id).collect();
        assert_eq!(ids, ["bbbb-0002", "cccc-0003", "aaaa-0001"]);
        assert!(dir.join("dddd-0004.json.tmp").exists(), "a .tmp file is being written and is left alone");
        assert!(dir.join("notes.txt").exists(), "other files are left alone");
        assert!(dir.join("folder-0005.json").is_dir(), "folders are left alone");
        assert!(!dir.join("short.json").exists(), "a .json file with a bad name is deleted");
        assert!(!dir.join("bad_name_1.json").exists(), "a .json file with a bad name is deleted");

        // Taking a request deletes it, readable or not
        let (id, path) = pending_requests(base).remove(0);
        assert_eq!(take_request(&path, &id), Taken::Request(Request::Library));
        assert!(!path.exists());
        let broken = dir.join("eeee-0006.json");
        std::fs::write(&broken, "{ half a fi").unwrap();
        assert_eq!(take_request(&broken, "eeee-0006"), Taken::Unreadable);
        assert!(!broken.exists());
    }

    #[test]
    fn atomic_writes_leave_no_tmp_file() {
        let t = TempDir::new("atomic");
        let path = t.path().join("responses").join("req-0001.json");
        write_atomic(&path, b"{\"a\": 1}").unwrap();
        write_atomic(&path, b"{\"a\": 2}").unwrap();
        assert_eq!(std::fs::read_to_string(&path).unwrap(), "{\"a\": 2}");
        let names: Vec<String> = std::fs::read_dir(path.parent().unwrap())
            .unwrap()
            .map(|e| e.unwrap().file_name().to_string_lossy().to_string())
            .collect();
        assert_eq!(names, ["req-0001.json"]);
    }

    #[test]
    fn a_module_lives_under_its_project_and_version() {
        let base = Path::new("/Users/someone/Library/Application Support/Pangea/host");
        assert_eq!(
            module_dir(base, ID, 7),
            PathBuf::from(format!("/Users/someone/Library/Application Support/Pangea/host/modules/{ID}/v7"))
        );
        assert!(host_dir().unwrap().ends_with("Library/Application Support/Pangea/host"));
    }

    #[test]
    fn a_response_has_exactly_the_documented_fields() {
        let t = TempDir::new("response");
        let module = module_dir(t.path(), ID, 7);
        let r = Response::new("req-0001", Outcome::Ready, Some(ID), Some(7), Some(&module), "Tape Drift v7 is ready.");
        write_response(t.path(), &r).unwrap();
        let v: Value = serde_json::from_str(&std::fs::read_to_string(responses_dir(t.path()).join("req-0001.json")).unwrap()).unwrap();
        assert_eq!(keys(&v), ["format", "requestId", "ok", "outcome", "projectId", "version", "moduleDir", "message"]);
        assert_eq!(v["format"], 1);
        assert_eq!(v["ok"], true);
        assert_eq!(v["outcome"], "ready");
        assert_eq!(v["version"], 7);
        assert_eq!(v["moduleDir"], module.display().to_string());
        let refused = serde_json::to_value(Response::new("req-0002", Outcome::Refused, Some(ID), None, None, NOT_MINE_MESSAGE)).unwrap();
        assert_eq!(refused["ok"], false);
        assert_eq!(refused["outcome"], "refused");
        assert_eq!(refused["moduleDir"], Value::Null);
        assert_eq!(refused["version"], Value::Null);
        assert_eq!(serde_json::to_value(Outcome::Failed).unwrap(), "failed");
    }

    #[tokio::test]
    async fn library_json_has_exactly_the_documented_fields() {
        let t = TempDir::new("library");
        let mut no_name = project(OTHER_ID, "Glass Chorus", someone_else(), None);
        no_name.description = None;
        // Not filed: as the site sends a plugin made before decision 85.
        no_name.category = None;
        no_name.category_name = None;
        no_name.tags = Vec::new();
        no_name.keywords = Vec::new();
        no_name.summary = None;
        let fake = Fake::new(vec![project(ID, "Tape Drift", mine(), Some(7)), no_name], None);
        let library = refresh_library(Some(&fake), &mut LibraryMemory::default()).await;
        write_library(t.path(), &library).unwrap();
        let v: Value = serde_json::from_str(&std::fs::read_to_string(t.path().join("library.json")).unwrap()).unwrap();
        assert_eq!(keys(&v), ["format", "updatedAt", "appPid", "signedIn", "member", "error", "plugins"]);
        assert_eq!(v["format"], 1);
        assert_eq!(v["appPid"], std::process::id());
        assert_eq!(v["signedIn"], true);
        assert_eq!(v["member"], serde_json::json!({ "name": "Ada", "email": "ada@example.com" }));
        assert_eq!(v["error"], Value::Null);
        let at = v["updatedAt"].as_str().unwrap();
        assert!(at.ends_with('Z') && chrono::DateTime::parse_from_rfc3339(at).is_ok(), "{at}");
        let p = &v["plugins"][0];
        assert_eq!(
            keys(p),
            [
                "projectId", "name", "slug", "kind", "headVersion", "mine", "owner", "description", "updatedAt",
                "category", "categoryName", "tags", "keywords", "summary"
            ]
        );
        assert_eq!(p["category"], "delay");
        assert_eq!(p["categoryName"], "Delay");
        assert_eq!(p["tags"], serde_json::json!(["tape", "dub"]));
        assert_eq!(p["keywords"], serde_json::json!(["echo"]));
        assert_eq!(p["summary"], "A tape echo that wobbles.");
        assert_eq!(p["projectId"], ID);
        assert_eq!(p["slug"], "tape-drift");
        assert_eq!(p["kind"], "effect");
        assert_eq!(p["headVersion"], 7);
        assert_eq!(p["mine"], true);
        assert_eq!(p["owner"], "Ada");
        let other = &v["plugins"][1];
        assert_eq!(other["mine"], false);
        assert_eq!(other["owner"], "Sam");
        assert_eq!(other["headVersion"], Value::Null);
        assert_eq!(other["description"], Value::Null);
        assert_eq!(other["category"], Value::Null);
        assert_eq!(other["categoryName"], Value::Null);
        assert_eq!(other["tags"], serde_json::json!([]));
        assert_eq!(other["keywords"], serde_json::json!([]));
        assert_eq!(other["summary"], Value::Null);
    }

    #[tokio::test]
    async fn a_failed_refresh_keeps_the_last_list_and_signing_out_empties_it() {
        let fake = Fake::new(vec![project(ID, "Tape Drift", mine(), Some(7))], None);
        let mut memory = LibraryMemory::default();
        let first = refresh_library(Some(&fake), &mut memory).await;
        assert_eq!(first.plugins.len(), 1);

        *fake.projects.lock().unwrap() = Err("GET /api/desktop/projects: could not reach http://localhost:3000".into());
        let failed = refresh_library(Some(&fake), &mut memory).await;
        assert!(failed.signed_in);
        assert_eq!(failed.plugins, first.plugins);
        assert_eq!(failed.member, first.member);
        assert_eq!(failed.error.as_deref(), Some(REFRESH_FAILED));

        let out = refresh_library(None::<&Fake>, &mut memory).await;
        assert!(!out.signed_in);
        assert!(out.plugins.is_empty() && out.member.is_none() && out.error.is_none());
        let v = serde_json::to_value(&out).unwrap();
        assert_eq!(v["member"], Value::Null);
        assert_eq!(v["plugins"], serde_json::json!([]));
    }

    #[tokio::test]
    async fn a_library_request_writes_the_list_then_answers_ready() {
        let t = TempDir::new("library-request");
        let env = env(&t, "");
        let fake = Fake::new(vec![project(ID, "Tape Drift", mine(), Some(7))], None);
        let r = handle_request(Some(&fake), &env, &mut LibraryMemory::default(), "req-0001", &Request::Library, &QuietLog).await;
        assert_eq!(r.outcome, Outcome::Ready);
        assert!(r.ok);
        assert_eq!((r.project_id, r.version, r.module_dir), (None, None, None));
        assert!(env.base.join("library.json").is_file());
        assert!(!env.base.join("library.json.tmp").exists());
    }

    #[tokio::test]
    async fn a_built_module_is_ready_at_once_without_fetching() {
        let t = TempDir::new("cache");
        let env = env(&t, "");
        let module = module_dir(&env.base, ID, 7);
        std::fs::create_dir_all(&module).unwrap();
        std::fs::write(module.join("manifest.json"), "{}").unwrap();
        let fake = Fake::new(vec![project(ID, "Tape Drift", mine(), Some(7))], Some(bundle(7, None)));
        let request = Request::Module { project_id: ID.into(), version: None, rebuild: false };
        let r = handle_request(Some(&fake), &env, &mut LibraryMemory::default(), "req-0001", &request, &QuietLog).await;
        assert_eq!(r.outcome, Outcome::Ready);
        assert_eq!(r.version, Some(7));
        assert_eq!(r.project_id.as_deref(), Some(ID));
        assert_eq!(r.module_dir, Some(module.display().to_string()));
        assert_eq!(r.message, "Tape Drift v7 is ready.");
        assert_eq!(fake.fetches.load(Ordering::SeqCst), 0, "nothing is fetched for a module already built");
    }

    #[tokio::test]
    async fn someone_elses_plugin_is_refused() {
        let t = TempDir::new("not-mine");
        let env = env(&t, "");
        let fake = Fake::new(vec![project(ID, "Glass Chorus", someone_else(), Some(3))], Some(bundle(3, None)));
        let request = Request::Module { project_id: ID.into(), version: None, rebuild: false };
        let r = handle_request(Some(&fake), &env, &mut LibraryMemory::default(), "req-0001", &request, &QuietLog).await;
        assert_eq!(r.outcome, Outcome::Refused);
        assert!(!r.ok);
        assert_eq!(r.message, "For now the Pangea plugin opens your own plugins. Install this one from the Pangea app.");
        assert_eq!(r.module_dir, None);
        assert_eq!(fake.fetches.load(Ordering::SeqCst), 0);
        assert!(!env.base.join("modules").join(ID).exists());

        // A version that names another owner is refused too, even when the list says it is mine
        let mut theirs = bundle(3, None);
        theirs.owner = someone_else();
        let fake = Fake::new(vec![project(ID, "Glass Chorus", mine(), Some(3))], Some(theirs));
        let shell = TempDir::new("not-mine-shell");
        std::fs::write(shell.path().join("build-module.sh"), "exit 1\n").unwrap();
        let env = HostEnv { shell_dir: shell.path().display().to_string(), ..env };
        let r = handle_request(Some(&fake), &env, &mut LibraryMemory::default(), "req-0002", &request, &QuietLog).await;
        assert_eq!(r.outcome, Outcome::Refused);
        assert_eq!(r.message, NOT_MINE_MESSAGE);
        assert!(!env.base.join("sources").exists(), "nothing was written");
    }

    #[tokio::test]
    async fn signed_out_or_unknown_or_versionless_requests_fail_plainly() {
        let t = TempDir::new("plain");
        let env = env(&t, "");
        let request = Request::Module { project_id: ID.into(), version: Some(2), rebuild: false };
        let r = handle_request(None::<&Fake>, &env, &mut LibraryMemory::default(), "req-0001", &request, &QuietLog).await;
        assert_eq!((r.outcome, r.message.as_str(), r.version), (Outcome::Failed, SIGN_IN_FIRST, Some(2)));

        let fake = Fake::new(vec![project(OTHER_ID, "Tape Drift", mine(), Some(7))], None);
        let r = handle_request(Some(&fake), &env, &mut LibraryMemory::default(), "req-0002", &request, &QuietLog).await;
        assert_eq!((r.outcome, r.message.as_str()), (Outcome::Failed, "This plugin is not in your Pangea list."));

        let fake = Fake::new(vec![project(ID, "Tape Drift", mine(), None)], None);
        let head = Request::Module { project_id: ID.into(), version: None, rebuild: false };
        let r = handle_request(Some(&fake), &env, &mut LibraryMemory::default(), "req-0003", &head, &QuietLog).await;
        assert_eq!((r.outcome, r.message.as_str()), (Outcome::Failed, "Tape Drift has no version yet."));
    }

    #[tokio::test]
    async fn a_shell_without_build_module_sh_fails_plainly() {
        let t = TempDir::new("no-script");
        let shell = TempDir::new("no-script-shell");
        let env = env(&t, &shell.path().display().to_string());
        let fake = Fake::new(vec![project(ID, "Tape Drift", mine(), Some(7))], Some(bundle(7, None)));
        let request = Request::Module { project_id: ID.into(), version: None, rebuild: false };
        let r = handle_request(Some(&fake), &env, &mut LibraryMemory::default(), "req-0001", &request, &QuietLog).await;
        assert_eq!(r.outcome, Outcome::Failed);
        assert!(r.message.starts_with("The shell has no build-module.sh"), "{}", r.message);
        assert_eq!(fake.fetches.load(Ordering::SeqCst), 0);
    }

    /// A stand-in for `build-module.sh` that records its arguments, writes a manifest and ends
    /// with the documented JSON line.
    const FAKE_BUILD: &str = r#"#!/bin/bash
set -e
printf '%s\n' "$@" > "$(dirname "$0")/args.txt"
mkdir -p "$2"
echo "compiling"
printf '{"format": 1}' > "$2/manifest.json"
echo "{\"ok\": true, \"stage\": \"done\", \"moduleDir\": \"$2\"}"
"#;

    const FAILING_BUILD: &str = r#"#!/bin/bash
echo "compiling"
echo '{"ok": false, "stage": "faust", "error": "Faust could not compile dsp.dsp."}'
exit 1
"#;

    #[tokio::test]
    async fn a_module_is_built_with_the_documented_arguments() {
        let t = TempDir::new("build");
        let shell = TempDir::new("build-shell");
        std::fs::write(shell.path().join("build-module.sh"), FAKE_BUILD).unwrap();
        let env = env(&t, &shell.path().display().to_string());
        let logic = "#include \"logic_api.h\"\nint counter = 0;\n";
        assert!(crate::safety::check_logic(logic).is_empty());
        let fake = Fake::new(vec![project(ID, "Tape Drift", mine(), Some(7))], Some(bundle(7, Some(logic))));
        let request = Request::Module { project_id: ID.into(), version: None, rebuild: false };
        let r = handle_request(Some(&fake), &env, &mut LibraryMemory::default(), "req-0001", &request, &QuietLog).await;
        let module = module_dir(&env.base, ID, 7);
        assert_eq!(r.outcome, Outcome::Ready, "{}", r.message);
        assert_eq!(r.message, "Tape Drift v7 is ready.");
        assert_eq!(r.module_dir, Some(module.display().to_string()));
        assert!(Path::new(r.module_dir.as_deref().unwrap()).is_absolute());

        let project_dir = sources_dir(&env.base, ID, 7);
        assert!(project_dir.join("dsp.dsp").is_file() && project_dir.join("logic.cpp").is_file());
        let args = std::fs::read_to_string(shell.path().join("args.txt")).unwrap();
        let expected = [
            project_dir.display().to_string(),
            module.display().to_string(),
            "--project-id".into(),
            ID.into(),
            "--version-number".into(),
            "7".into(),
            "--allow-logic".into(),
        ];
        assert_eq!(args.lines().collect::<Vec<_>>(), expected);

        // A second request finds it built
        let again = handle_request(Some(&fake), &env, &mut LibraryMemory::default(), "req-0002", &request, &QuietLog).await;
        assert_eq!(again.outcome, Outcome::Ready);
        assert_eq!(fake.fetches.load(Ordering::SeqCst), 1);

        // A rebuild that fails says why and leaves no manifest behind
        std::fs::write(shell.path().join("build-module.sh"), FAILING_BUILD).unwrap();
        let rebuild = Request::Module { project_id: ID.into(), version: Some(7), rebuild: true };
        let failed = handle_request(Some(&fake), &env, &mut LibraryMemory::default(), "req-0003", &rebuild, &QuietLog).await;
        assert_eq!(failed.outcome, Outcome::Failed);
        assert_eq!(failed.message, "Faust could not compile dsp.dsp.");
        assert_eq!(failed.module_dir, None);
        assert!(!module.join("manifest.json").exists());
    }

    #[tokio::test]
    async fn a_version_the_checks_refuse_is_not_built() {
        let t = TempDir::new("refused");
        let shell = TempDir::new("refused-shell");
        std::fs::write(shell.path().join("build-module.sh"), FAKE_BUILD).unwrap();
        let env = env(&t, &shell.path().display().to_string());
        let mut bad = bundle(7, None);
        bad.faust = "process = ffunction(float foo(float), \"foo.h\", \"\");\n".into();
        let fake = Fake::new(vec![project(ID, "Tape Drift", mine(), Some(7))], Some(bad));
        let request = Request::Module { project_id: ID.into(), version: None, rebuild: false };
        let r = handle_request(Some(&fake), &env, &mut LibraryMemory::default(), "req-0001", &request, &QuietLog).await;
        assert_eq!(r.outcome, Outcome::Refused);
        assert!(r.message.starts_with("Not built: dsp.dsp:1:"), "{}", r.message);
        assert!(!env.base.join("sources").join(ID).exists(), "nothing was written");
        assert!(!shell.path().join("args.txt").exists(), "nothing was built");
    }

    #[test]
    fn the_messages_follow_the_copy_rules() {
        for text in [NOT_MINE_MESSAGE, REFRESH_FAILED, SESSION_EXPIRED, SIGN_IN_FIRST] {
            assert!(!text.contains('\u{2014}') && !text.contains('\u{2013}') && !text.contains('!'), "copy rules: {text}");
        }
    }
}
