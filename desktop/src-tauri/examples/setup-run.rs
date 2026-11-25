//! The app's setup, run for real without its window, into a scratch folder: what the Setup
//! screen does, step by step, printing what each step says and how long it took.
//!
//!   PANGEA_DESKTOP_HOME=<scratch home> PANGEA_DESKTOP_PAYLOAD=<staged payload> \
//!   PANGEA_DESKTOP_SYSTEM_PATH= cargo run --example setup-run
//!
//! `PANGEA_DESKTOP_HOME` is required, so this never touches the real Application Support or
//! plug-in folders. `PANGEA_DESKTOP_PAYLOAD` is the folder `desktop/scripts/stage-payload.sh`
//! makes (`desktop/src-tauri/target/pangea-payload`): what a packaged app carries.
//! `PANGEA_DESKTOP_SYSTEM_PATH` set to nothing leaves Homebrew off the path, as on a Mac that
//! has only Apple's tools. PROGRESS.md, 19 November 2026, keeps the output of the full run.

use std::time::Instant;

use pangea_desktop_lib::build::JobLog;
use pangea_desktop_lib::tools::{Fetch, Report};
use pangea_desktop_lib::{apple, host_install, paths, selfcheck, settings, toolchain, tools, AppState};

struct Print;
impl Report for Print {
    fn report(&self, f: &Fetch) {
        if f.stage == "download" {
            println!("    {} {}: {} of {} MB", f.tool, f.version, f.received / 1_000_000, f.total / 1_000_000);
        } else {
            println!("    {} {}: {} ({})", f.tool, f.version, f.stage, f.message);
        }
    }
}

fn step(n: u32, title: &str) -> Instant {
    println!("\n== {n}. {title}");
    Instant::now()
}

fn took(started: Instant) {
    println!("   ({:.1} s)", started.elapsed().as_secs_f64());
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    if !paths::OVERRIDES_HONOURED {
        anyhow::bail!("a release build ignores {}: run this in a development build (cargo run --example setup-run)", paths::HOME_OVERRIDE);
    }
    if !paths::is_scratch_home() {
        anyhow::bail!("set {} to a scratch folder: this run never uses the real home folder", paths::HOME_OVERRIDE);
    }
    let all = Instant::now();
    let state = AppState::default();
    println!("home: {}", paths::home()?.display());
    println!("support: {}", paths::support_dir()?.display());
    println!("PATH for builds: {}", pangea_desktop_lib::prereqs::tool_path());

    let t = step(1, "Apple's build tools");
    let found = apple::refresh(&state).await;
    println!("   {}: {}", found.state, found.message);
    println!("   compiler: {}", found.compiler.as_deref().unwrap_or("none"));
    println!("   developer folder: {}", found.developer_dir.as_deref().unwrap_or("none"));
    println!("   SDKROOT: {}", found.sdk.as_deref().unwrap_or("(the default works)"));
    took(t);
    if !found.is_ready() {
        anyhow::bail!("Apple's build tools are not ready: {}", found.details);
    }

    let t = step(2, "What the app carries, unpacked");
    let unpacked = toolchain::prepare(&state, None, None)?;
    match &unpacked {
        Some(tc) => println!("   {} (Faust {}, Ninja {})", tc.root.display(), tc.manifest.faust.version, tc.manifest.ninja.version),
        None => anyhow::bail!("no payload: set {}", toolchain::PAYLOAD_OVERRIDE),
    }
    let shell = settings::shell_info(&settings::Settings { api_base_url: String::new(), shell_dir: String::new() }, unpacked.as_ref());
    println!("   shell: {} ({}, version {})", shell.dir, shell.source, shell.version.as_deref().unwrap_or("none"));
    took(t);

    let t = step(3, "CMake and JUCE, fetched and checked");
    let support = paths::support_dir()?;
    println!("   needed before: {:?}", tools::needs(&support, &shell));
    let left = tools::ensure(&state, &shell, &Print).await?;
    println!("   needed after: {left:?}");
    println!("   PATH for builds: {}", pangea_desktop_lib::prereqs::tool_path());
    for tool in ["faust", "ninja", "cmake"] {
        println!("   {tool}: {}", pangea_desktop_lib::prereqs::find_tool(tool).map(|p| p.display().to_string()).unwrap_or_else(|| "NOT FOUND".into()));
    }
    took(t);

    let t = step(4, "The Pangea plugin, built, installed and checked");
    let plugins = paths::plugins_dir()?;
    let shell_dir = std::path::Path::new(&shell.dir);
    println!("   before: {:?}", host_install::detect(&plugins, shell_dir, &support));
    println!("   DAWs open now (not in the way of a scratch run): {:?}", host_install::running_daws());
    let report = host_install::install(shell_dir, &plugins, &support, &Lines, &Lines).await;
    println!("   {}: {} (stage {}, {:.0} s)", if report.ok { "ok" } else { "FAILED" }, report.message, report.stage, report.seconds);
    for (spec, verdict) in &report.auval {
        println!("   auval {spec}: {verdict}");
    }
    let after = host_install::detect(&plugins, shell_dir, &support);
    println!("   after: {after:?}");
    for b in &host_install::BUNDLES {
        let p = host_install::installed_path(&plugins, b);
        println!(
            "   {}: {}, quarantine {}",
            p.display(),
            host_install::bundle_version(&p).unwrap_or_else(|| "MISSING".into()),
            if toolchain::has_quarantine(&p) { "SET" } else { "none" }
        );
    }
    took(t);
    if !report.ok {
        anyhow::bail!("the Pangea plugin did not install");
    }

    let t = step(5, "Check it works: a test plugin, opened by the Pangea plugin");
    let check = selfcheck::run(shell_dir, &plugins, &support, &Lines, &Lines).await;
    println!("   {}: {} (stage {}, {:.0} s)", if check.ok { "ok" } else { "FAILED" }, check.message, check.stage, check.seconds);
    println!("   what it cannot see: {}", selfcheck::CANNOT);
    took(t);
    if !check.ok {
        anyhow::bail!("the self-check did not pass");
    }

    println!("\nall steps: {:.1} s", all.elapsed().as_secs_f64());
    Ok(())
}

/// The build's own lines, shortened: the stages and the last words of each script.
struct Lines;
impl JobLog for Lines {
    fn push(&self, stream: &str, line: &str) {
        if stream == "app" || line.starts_with('[') || line.starts_with("error") || line.starts_with('{') {
            println!("    {line}");
        }
    }
}
impl host_install::Stages for Lines {
    fn stage(&self, stage: &str) {
        println!("   stage: {stage}");
    }
}
