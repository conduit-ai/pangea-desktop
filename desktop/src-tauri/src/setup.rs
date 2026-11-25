//! The Setup screen (the plan's section 6): five steps, each done, doing, needs you, failed or
//! waiting, with a time estimate, and one main button that does the next thing.
//!
//! 1. Apple's build tools (apple.rs)          2. Sign in
//! 3. Pangea's tools (toolchain.rs, tools.rs)  4. The Pangea plugin (host_install.rs)
//! 5. Check it works (selfcheck.rs, and the first request from the Pangea plugin in a DAW)
//!
//! `plan` decides everything from what the app knows (`Inputs`) and touches nothing, so the
//! states are unit tests; `gather` collects the inputs. A member who already has everything
//! (a developer with Homebrew's CMake, JUCE in their own shell and the Pangea plugin installed
//! in Terminal) sees five ticks and is not made to install anything again.

use std::path::Path;

use serde::Serialize;

use crate::apple::{self, AppleTools};
use crate::host_install::{self, HostPlugin, InstallReport};
use crate::selfcheck::{self, CheckReport};
use crate::settings::ShellInfo;
use crate::toolchain::Unpack;
use crate::tools::{self, Fetch, Needs};
use crate::{host, paths, AppState};

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct Action {
    /// What the page does: `install-apple`, `open-xcode`, `check-apple`, `sign-in`, `get-tools`,
    /// `open-settings`, `install-plugin`, `install-plugin-anyway`, `run-check`.
    pub id: String,
    pub label: String,
}

fn action(id: &str, label: &str) -> Option<Action> {
    Some(Action { id: id.into(), label: label.into() })
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct Step {
    pub id: String,
    pub title: String,
    /// `done`, `doing`, `needs-you`, `failed` or `waiting`.
    pub state: String,
    pub detail: String,
    /// How long it usually takes, while it is still to do.
    pub estimate: Option<String>,
    pub action: Option<Action>,
    /// A second, quieter button (Install anyway, Check again, Update).
    pub extra: Option<Action>,
    /// A page to open, such as Apple's download page when Apple's installer stopped.
    pub link: Option<Link>,
    /// What Copy details adds: the compiler's own words, an error in full.
    pub details: String,
    /// The check step only: the later tick in the DAW, apart from what the button's own check
    /// found. `done` once the Pangea plugin has asked the app for the member's plugins.
    pub daw: Option<DawLine>,
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct DawLine {
    /// `done` or `waiting`.
    pub state: String,
    pub text: String,
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct Link {
    pub label: String,
    pub url: String,
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct SetupStatus {
    pub steps: Vec<Step>,
    /// Every step has its tick.
    pub complete: bool,
    /// What the main button does: the first step that needs the member.
    pub next: Option<Action>,
    /// Something is under way, so the page looks again soon.
    pub busy: bool,
    /// When the Pangea plugin last asked the app from a DAW, "14:02".
    pub daw_seen: Option<String>,
}

/// Everything the plan is made from.
#[derive(Debug, Clone)]
pub struct Inputs {
    pub apple: AppleTools,
    pub signed_in: bool,
    /// The app carries its own Faust, Ninja and shell (a packaged app).
    pub carries: bool,
    /// How unpacking them ended (`toolchain::prepare`).
    pub unpack: Unpack,
    /// A development build, which carries nothing and may name the repository's shell.
    pub development: bool,
    pub shell: ShellInfo,
    pub needs: Needs,
    pub fetching: bool,
    pub fetch: Option<Fetch>,
    pub host: HostPlugin,
    pub installing: bool,
    pub last_install: Option<InstallReport>,
    pub checking: bool,
    pub last_check: Option<CheckReport>,
    /// The Pangea plugin has built modules here before.
    pub used_before: bool,
    pub daw_seen: Option<String>,
}

fn step(id: &str, title: &str, state: &str, detail: impl Into<String>) -> Step {
    Step {
        id: id.into(),
        title: title.into(),
        state: state.into(),
        detail: detail.into(),
        estimate: None,
        action: None,
        extra: None,
        link: None,
        details: String::new(),
        daw: None,
    }
}

fn with(mut s: Step, estimate: Option<&str>, action: Option<Action>, extra: Option<Action>) -> Step {
    s.estimate = estimate.map(str::to_string);
    s.action = action;
    s.extra = extra;
    s
}

fn apple_step(a: &AppleTools) -> Step {
    let title = "Apple's build tools";
    let est = Some("5 to 30 minutes");
    match a.state.as_str() {
        "ready" => step("apple", title, "done", format!("Ready: {}.", a.compiler.as_deref().unwrap_or("Apple clang"))),
        "installing" => with(step("apple", title, "doing", &a.message), est, None, action("check-apple", "Check again")),
        "missing" => with(step("apple", title, "needs-you", &a.message), est, action("install-apple", "Install Apple's build tools"), None),
        "licence" => with(step("apple", title, "needs-you", &a.message), None, action("open-xcode", "Open Xcode"), action("check-apple", "Check again")),
        _ => {
            let mut s = with(step("apple", title, "failed", &a.message), est, action("install-apple", "Install Apple's build tools"), action("check-apple", "Check again"));
            s.link = a.help_url.clone().map(|url| Link { label: "Apple's download page".into(), url });
            s.details = a.details.clone();
            s
        }
    }
}

fn tools_step(i: &Inputs) -> Step {
    let title = "Pangea's tools";
    match &i.unpack {
        Unpack::Pending => return step("tools", title, "doing", "Getting ready: Pangea is unpacking the tools it carries."),
        Unpack::Failed { message, details } => {
            let mut s = with(step("tools", title, "failed", message), None, action("unpack-again", "Try again"), None);
            s.details = details.clone();
            return s;
        }
        Unpack::NoPayload if !i.development => {
            return step(
                "tools",
                title,
                "failed",
                "This copy of Pangea is missing the tools it carries. Quit Pangea, download it again, move it to Applications and open it from there.",
            )
        }
        _ => {}
    }
    if i.shell.source == "none" {
        return with(
            step("tools", title, "failed", "This development build carries no shell. Name the repository's shell folder in Settings, For developers."),
            None,
            action("open-settings", "Open Settings"),
            None,
        );
    }
    if i.fetching {
        let line = i.fetch.as_ref().map(fetch_line).unwrap_or_else(|| "Getting Pangea's tools".into());
        return step("tools", title, "doing", line);
    }
    if i.needs.nothing() {
        if i.needs.juce_in_own_shell_missing {
            return step("tools", title, "failed", "Your own shell folder has no JUCE. Pangea does not write into it: clone JUCE there, or clear the folder in Settings to use Pangea's own.");
        }
        let carried = if i.carries { "Faust and Ninja come with Pangea; " } else { "" };
        return step("tools", title, "done", format!("{carried}CMake and JUCE are ready."));
    }
    let what = match (i.needs.cmake, i.needs.juce) {
        (true, true) => "CMake and JUCE, about 130 MB",
        (true, false) => "CMake, about 80 MB",
        _ => "JUCE, about 50 MB",
    };
    if let Some(f) = i.fetch.as_ref().filter(|f| f.stage == "failed") {
        return with(step("tools", title, "failed", &f.message), Some("1 to 3 minutes"), action("get-tools", "Try again"), None);
    }
    with(
        step("tools", title, "needs-you", format!("Pangea fetches {what} from their makers' own releases and checks each before it is used.")),
        Some("1 to 3 minutes"),
        action("get-tools", "Get Pangea's tools"),
        None,
    )
}

/// "Downloading CMake 3.31.12: 34 of 80 MB", or the step's own words.
pub fn fetch_line(f: &Fetch) -> String {
    if f.stage == "download" && f.total > 0 {
        format!("Downloading {} {}: {} of {} MB", f.tool, f.version, f.received / 1_000_000, (f.total + 500_000) / 1_000_000)
    } else {
        f.message.clone()
    }
}

fn plugin_step(i: &Inputs, before_done: bool) -> Step {
    let title = "The Pangea plugin";
    let est = Some("2 to 5 minutes the first time, seconds after");
    if i.installing {
        return step("plugin", title, "doing", "Building and installing the Pangea plugin.");
    }
    if !before_done {
        return step("plugin", title, "waiting", "After Apple's build tools and Pangea's tools.");
    }
    let installed_now = i.last_install.as_ref().is_some_and(|r| r.ok);
    // A failed install (Apple's check included) comes before what the folders show: the bundles
    // may be there and still not open in a DAW.
    if let Some(r) = i.last_install.as_ref().filter(|r| !r.ok && r.stage != "daw") {
        return with(step("plugin", title, "failed", &r.message), est, action("install-plugin", "Try again"), None);
    }
    match (i.host.state.as_str(), &i.last_install) {
        ("check-failed", _) => with(step("plugin", title, "failed", &i.host.message), est, action("install-plugin", "Install the Pangea plugin again"), None),
        ("installed", _) | ("update-available", _) => {
            let detail = if installed_now { host_install::RESCAN_MESSAGE.to_string() } else { i.host.message.clone() };
            let extra = (i.host.state == "update-available").then(|| Action { id: "install-plugin".into(), label: "Update the Pangea plugin".into() });
            with(step("plugin", title, "done", detail), None, None, extra)
        }
        (_, Some(r)) if r.stage == "daw" => {
            with(step("plugin", title, "needs-you", &r.message), est, action("install-plugin", "Install the Pangea plugin"), action("install-plugin-anyway", "Install anyway"))
        }
        ("update-needed", _) => with(step("plugin", title, "needs-you", &i.host.message), est, action("install-plugin", "Update the Pangea plugin"), None),
        _ => with(step("plugin", title, "needs-you", &i.host.message), est, action("install-plugin", "Install the Pangea plugin"), None),
    }
}

fn check_step(i: &Inputs, plugin_done: bool) -> Step {
    let daw = Some(match &i.daw_seen {
        Some(at) => DawLine { state: "done".into(), text: format!("Pangea is working in your DAW: the Pangea plugin asked for your plugins at {at}.") },
        None => DawLine { state: "waiting".into(), text: selfcheck::CANNOT.into() },
    });
    let mut s = check_state(i, plugin_done);
    if s.state != "waiting" {
        s.daw = daw;
    }
    s
}

fn check_state(i: &Inputs, plugin_done: bool) -> Step {
    let title = "Check it works";
    let est = Some("2 to 4 minutes");
    if i.checking {
        return step("check", title, "doing", "Building a test plugin and opening it in the Pangea plugin. A Pangea window shows for a few seconds.");
    }
    if !plugin_done {
        return step("check", title, "waiting", "After the Pangea plugin.");
    }
    match &i.last_check {
        Some(r) if r.ok => with(step("check", title, "done", selfcheck::PROVES), None, None, action("run-check", "Check again")),
        Some(r) => with(step("check", title, "failed", &r.message), est, action("run-check", "Try again"), None),
        None if i.used_before || i.daw_seen.is_some() => with(
            step("check", title, "done", "The Pangea plugin has opened your plugins on this Mac before."),
            None,
            None,
            action("run-check", "Check again"),
        ),
        None => with(
            step("check", title, "needs-you", "Press Check it works: Pangea builds a small test plugin and the Pangea plugin opens it."),
            est,
            action("run-check", "Check it works"),
            None,
        ),
    }
}

/// The five steps and the main button, from what the app knows.
pub fn plan(i: &Inputs) -> SetupStatus {
    let apple = apple_step(&i.apple);
    let signin = if i.signed_in {
        step("signin", "Sign in", "done", "Signed in.")
    } else {
        with(
            step("signin", "Sign in", "needs-you", "Sign in with your Pangea account in the browser. You can do this while Apple's tools install."),
            Some("1 minute"),
            action("sign-in", "Sign in"),
            None,
        )
    };
    let tools = tools_step(i);
    let plugin = plugin_step(i, apple.state == "done" && tools.state == "done");
    let check = check_step(i, plugin.state == "done");
    let steps = vec![apple, signin, tools, plugin, check];
    let complete = steps.iter().all(|s| s.state == "done");
    let next = steps.iter().find(|s| s.state == "needs-you" || s.state == "failed").and_then(|s| s.action.clone());
    let busy = steps.iter().any(|s| s.state == "doing");
    SetupStatus { steps, complete, next, busy, daw_seen: i.daw_seen.clone() }
}

/// What the app knows now. Apple's tools are checked again only while they are not ready, so
/// the end of Apple's installer turns to a tick by itself.
pub async fn gather(state: &AppState, shell: ShellInfo, signed_in: bool) -> Inputs {
    let known = apple::current(state).await;
    let apple = if known.is_ready() { known } else { apple::refresh(state).await };
    let support = paths::support_dir().unwrap_or_default();
    let plugins = paths::plugins_dir().unwrap_or_default();
    let used_before = host::host_dir().map(|h| !host::modules_on_disk(&h).is_empty()).unwrap_or(false);
    Inputs {
        apple,
        signed_in,
        carries: state.toolchain.lock().unwrap().is_some(),
        unpack: state.unpack.lock().unwrap().clone(),
        development: cfg!(debug_assertions),
        needs: tools::needs(&support, &shell),
        host: host_install::detect(&plugins, Path::new(&shell.dir), &support),
        shell,
        fetching: state.fetching.load(std::sync::atomic::Ordering::SeqCst),
        fetch: state.fetch_progress.lock().unwrap().clone(),
        installing: state.installing_host.load(std::sync::atomic::Ordering::SeqCst),
        last_install: state.last_install.lock().unwrap().clone(),
        checking: state.checking.load(std::sync::atomic::Ordering::SeqCst),
        last_check: state.last_check.lock().unwrap().clone(),
        used_before,
        daw_seen: state.plugin_seen.lock().unwrap().clone(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn apple(state: &str) -> AppleTools {
        AppleTools {
            state: state.into(),
            message: format!("apple {state}"),
            developer_dir: None,
            developer_dir_override: None,
            sdk: None,
            compiler: Some("Apple clang version 21.0.0".into()),
            details: String::new(),
            estimate: None,
            help_url: None,
        }
    }

    fn host(state: &str) -> HostPlugin {
        HostPlugin { state: state.into(), message: format!("plugin {state}"), installed_version: None, wanted_version: Some("0.2.0".into()), by_app: false }
    }

    /// A fresh Mac: nothing there yet.
    fn fresh() -> Inputs {
        Inputs {
            apple: apple("missing"),
            signed_in: false,
            carries: true,
            unpack: Unpack::Ready,
            development: false,
            shell: ShellInfo { source: "app".into(), dir: "/x/shell".into(), version: Some("abc".into()) },
            needs: Needs { cmake: true, juce: true, juce_in_own_shell_missing: false },
            fetching: false,
            fetch: None,
            host: host("missing"),
            installing: false,
            last_install: None,
            checking: false,
            last_check: None,
            used_before: false,
            daw_seen: None,
        }
    }

    fn states(s: &SetupStatus) -> Vec<&str> {
        s.steps.iter().map(|s| s.state.as_str()).collect()
    }

    #[test]
    fn a_fresh_mac_starts_with_apples_tools_and_can_sign_in_meanwhile() {
        let s = plan(&fresh());
        assert_eq!(states(&s), vec!["needs-you", "needs-you", "needs-you", "waiting", "waiting"]);
        assert_eq!(s.next.as_ref().map(|a| a.id.as_str()), Some("install-apple"));
        assert_eq!(s.steps[0].estimate.as_deref(), Some("5 to 30 minutes"));
        assert!(!s.complete);

        // While Apple's installer runs, the main button moves on to signing in.
        let mut i = fresh();
        i.apple = apple("installing");
        let s = plan(&i);
        assert_eq!(states(&s)[0], "doing");
        assert!(s.busy);
        assert_eq!(s.next.as_ref().map(|a| a.id.as_str()), Some("sign-in"));
    }

    #[test]
    fn each_step_leads_to_the_next() {
        let mut i = fresh();
        i.apple = apple("ready");
        i.signed_in = true;
        assert_eq!(plan(&i).next.as_ref().map(|a| a.label.as_str()), Some("Get Pangea's tools"));

        i.fetching = true;
        i.fetch = Some(Fetch { tool: "CMake".into(), version: "3.31.12".into(), stage: "download".into(), received: 34_000_000, total: 79_954_716, message: String::new() });
        let s = plan(&i);
        assert_eq!(s.steps[2].detail, "Downloading CMake 3.31.12: 34 of 80 MB");
        assert_eq!(states(&s)[3], "waiting");

        i.fetching = false;
        i.fetch = None;
        i.needs = Needs { cmake: false, juce: false, juce_in_own_shell_missing: false };
        let s = plan(&i);
        assert_eq!(s.next.as_ref().map(|a| a.label.as_str()), Some("Install the Pangea plugin"));

        i.host = host("installed");
        i.last_install = Some(InstallReport { ok: true, stage: "done".into(), message: host_install::RESCAN_MESSAGE.into(), auval: vec![], seconds: 90.0 });
        let s = plan(&i);
        assert_eq!(s.steps[3].detail, host_install::RESCAN_MESSAGE);
        assert_eq!(s.next.as_ref().map(|a| a.label.as_str()), Some("Check it works"));

        i.last_check = Some(CheckReport { ok: true, stage: "done".into(), message: selfcheck::PROVES.into(), seconds: 150.0 });
        let s = plan(&i);
        assert!(s.complete);
        assert!(s.next.is_none());
        // What the button's check found, and apart from it the tick still to come in the DAW.
        assert_eq!(s.steps[4].detail, selfcheck::PROVES);
        let daw = s.steps[4].daw.as_ref().expect("the DAW's line");
        assert_eq!((daw.state.as_str(), daw.text.as_str()), ("waiting", selfcheck::CANNOT));

        i.daw_seen = Some("14:02".into());
        let s = plan(&i);
        assert_eq!(s.steps[4].detail, selfcheck::PROVES);
        let daw = s.steps[4].daw.as_ref().unwrap();
        assert_eq!(daw.state, "done");
        assert!(daw.text.starts_with("Pangea is working in your DAW"), "{}", daw.text);
    }

    #[test]
    fn a_member_who_has_everything_sees_five_ticks() {
        let i = Inputs {
            apple: apple("ready"),
            signed_in: true,
            carries: false,
            shell: ShellInfo { source: "own".into(), dir: "/Users/someone/Pangea/shell".into(), version: None },
            needs: Needs { cmake: false, juce: false, juce_in_own_shell_missing: false },
            host: host("installed"),
            used_before: true,
            ..fresh()
        };
        let s = plan(&i);
        assert_eq!(states(&s), vec!["done"; 5]);
        assert!(s.complete && s.next.is_none());
        assert!(s.steps.iter().all(|s| s.action.is_none()), "nothing asks to be installed again");
    }

    #[test]
    fn failures_and_the_daw_ask_for_the_member() {
        let mut i = fresh();
        i.apple = apple("licence");
        assert_eq!(plan(&i).next.as_ref().map(|a| a.id.as_str()), Some("open-xcode"));
        i.apple = apple("stopped");
        assert_eq!(states(&plan(&i))[0], "failed");

        i.apple = apple("ready");
        i.signed_in = true;
        i.fetch = Some(Fetch { tool: "JUCE".into(), version: "8.0.10".into(), stage: "failed".into(), received: 0, total: 1, message: "Pangea could not reach github.com to fetch JUCE.".into() });
        let s = plan(&i);
        assert_eq!((s.steps[2].state.as_str(), s.next.as_ref().map(|a| a.label.as_str())), ("failed", Some("Try again")));

        i.fetch = None;
        i.needs = Needs { cmake: false, juce: false, juce_in_own_shell_missing: false };
        i.last_install = Some(host_install::close_daws_first(&["Logic Pro".to_string()]));
        let s = plan(&i);
        assert_eq!(s.steps[3].state, "needs-you");
        assert_eq!(s.steps[3].extra.as_ref().map(|a| a.id.as_str()), Some("install-plugin-anyway"));

        i.host = host("update-needed");
        i.last_install = None;
        assert_eq!(plan(&i).next.as_ref().map(|a| a.label.as_str()), Some("Update the Pangea plugin"));
    }

    /// The first status of a packaged app comes while it is still unpacking its tools: it says it
    /// is getting ready, and a failure is said in the member's words, never a developer's.
    #[test]
    fn the_first_launch_says_getting_ready_not_a_developers_message() {
        let none = ShellInfo { source: "none".into(), dir: String::new(), version: None };
        let mut i = fresh();
        i.shell = none.clone();
        i.carries = false;

        i.unpack = Unpack::Pending;
        let s = plan(&i);
        assert_eq!(s.steps[2].state, "doing");
        assert!(s.steps[2].detail.starts_with("Getting ready"), "{}", s.steps[2].detail);
        assert!(s.busy);

        i.unpack = Unpack::Failed {
            message: "Pangea could not set up the tools it carries. There is not enough free space on your Mac. Free some space, then press Try again.".into(),
            details: "could not copy to /x: No space left on device (os error 28)".into(),
        };
        let s = plan(&i);
        assert_eq!(s.steps[2].state, "failed");
        assert!(s.steps[2].detail.contains("not enough free space"), "{}", s.steps[2].detail);
        assert_eq!(s.steps[2].action.as_ref().map(|a| a.id.as_str()), Some("unpack-again"));

        i.unpack = Unpack::NoPayload;
        let s = plan(&i);
        assert_eq!(s.steps[2].state, "failed");
        assert!(s.steps[2].detail.contains("download it again"), "{}", s.steps[2].detail);

        for state in [Unpack::Pending, i.unpack.clone()] {
            let mut j = i.clone();
            j.unpack = state;
            let text = &plan(&j).steps[2].detail;
            assert!(!text.contains("repository") && !text.contains("shell folder") && !text.contains("development"), "{text}");
        }

        // Only a development build, which carries nothing, names the repository's shell.
        i.development = true;
        assert!(plan(&i).steps[2].detail.contains("development build"));
    }

    /// When Apple's step stops, it offers Apple's download page it speaks of, and Copy details
    /// carries what the compiler said.
    #[test]
    fn a_stopped_apple_step_links_apples_page_and_keeps_the_compilers_words() {
        let mut i = fresh();
        let mut stopped = apple("stopped");
        stopped.help_url = Some(apple::APPLE_DOWNLOADS.into());
        stopped.details = "DEVELOPER_DIR=(selected) SDKROOT=(default):\nld: tapi error: malformed file".into();
        i.apple = stopped;
        let s = plan(&i);
        let link = s.steps[0].link.as_ref().expect("a link to Apple's download page");
        assert_eq!((link.label.as_str(), link.url.as_str()), ("Apple's download page", apple::APPLE_DOWNLOADS));
        assert!(s.steps[0].details.contains("tapi error"), "{}", s.steps[0].details);
        assert!(plan(&fresh()).steps[0].link.is_none(), "no link before anything stopped");
    }

    /// The bundles are in place and their version is right, but Apple's check did not pass: no tick.
    #[test]
    fn a_failed_apple_check_is_not_a_tick() {
        let mut i = fresh();
        i.apple = apple("ready");
        i.signed_in = true;
        i.needs = Needs { cmake: false, juce: false, juce_in_own_shell_missing: false };
        i.host = host("installed");
        i.last_install = Some(InstallReport {
            ok: false,
            stage: "auval".into(),
            message: "Apple's check of Pangea Instrument did not pass.".into(),
            auval: vec![("aumu Phin Pnga".into(), "failed".into())],
            seconds: 70.0,
        });
        let s = plan(&i);
        assert_eq!(s.steps[3].state, "failed");
        assert_eq!(s.steps[3].action.as_ref().map(|a| a.id.as_str()), Some("install-plugin"));
        assert_eq!(s.steps[4].state, "waiting", "no self-check on a plugin Apple's check refused");
        assert!(!s.complete);

        // After the app starts again, the note on disk says the same.
        i.last_install = None;
        i.host = host("check-failed");
        assert_eq!(plan(&i).steps[3].state, "failed");
    }

    #[test]
    fn every_sentence_follows_the_copy_rules() {
        let mut i = fresh();
        i.daw_seen = Some("14:02".into());
        for s in plan(&i).steps {
            for text in [s.title, s.detail] {
                assert!(!text.contains('\u{2014}') && !text.contains('\u{2013}') && !text.contains('!'), "copy rules: {text}");
            }
        }
    }
}
