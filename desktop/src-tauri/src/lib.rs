//! Pangea desktop app. The page (React) is thin; everything that touches the
//! network, the keychain, the file system or a child process lives here and is
//! exposed as Tauri commands. The bearer token never reaches the page.

mod api;
mod auth;
mod commands;
mod deep_link;
mod host;
mod keychain;
mod safety;
mod setup;
mod state;

// Public for `examples/setup-run.rs`, which runs the setup steps for real into a scratch folder.
pub mod apple;
pub mod build;
pub mod host_install;
pub mod paths;
pub mod pins;
pub mod prereqs;
pub mod selfcheck;
pub mod settings;
pub mod toolchain;
pub mod tools;

pub use state::AppState;
use tauri::Manager;

pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_store::Builder::new().build())
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_deep_link::init())
        .manage(AppState::default())
        .invoke_handler(tauri::generate_handler![
            commands::get_settings,
            commands::set_settings,
            commands::default_settings,
            commands::check_prerequisites,
            commands::apple_tools,
            commands::apple_tools_install,
            commands::open_xcode,
            commands::auth_status,
            commands::sign_in_start,
            commands::sign_in_wait,
            commands::sign_in_cancel,
            commands::sign_out,
            commands::list_projects,
            commands::open_project_on_web,
            commands::open_url,
            commands::build_and_install,
            commands::build_state,
            commands::pending_confirm,
            commands::confirm_install,
            commands::clear_build_history,
            commands::take_pending_installs,
            commands::shell_info,
            commands::projects_dir,
            commands::about,
            commands::open_licences,
            commands::get_tools,
            commands::tools_needs,
            commands::host_plugin,
            commands::install_host_plugin,
            commands::run_self_check,
            commands::setup_status,
            commands::retry_unpack,
        ])
        .setup(|app| {
            deep_link::install(app);
            // The tools the app carries are unpacked and put first on every build's PATH, and
            // Apple's build tools are checked, once at start, before anything is built.
            let handle = app.handle().clone();
            tauri::async_runtime::spawn(async move {
                let state = handle.state::<AppState>();
                let resources = handle.path().resource_dir().ok();
                let exe_dir = std::env::current_exe().ok().and_then(|p| p.parent().map(std::path::Path::to_path_buf));
                if let Err(e) = toolchain::prepare(&state, resources.as_deref(), exe_dir.as_deref()) {
                    // Kept in the app's state too (toolchain::Unpack): Setup says it in the member's words.
                    eprintln!("[pangea] the tools the app carries could not be unpacked: {e:#}");
                }
                if let Ok(s) = settings::load(&handle) {
                    let toolchain = state.toolchain.lock().unwrap().clone();
                    if let Err(e) = tools::apply(&state, &settings::shell_info(&s, toolchain.as_ref())) {
                        eprintln!("[pangea] the app's tools could not be put in place: {e:#}");
                    }
                }
                apple::refresh(&state).await;
            });
            host::start(app.handle());
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("error while running Pangea");
}
