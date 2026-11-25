use std::sync::atomic::AtomicBool;
use std::sync::{Arc, Mutex};

use tokio::sync::oneshot;

use crate::apple::{AppleTools, InstallWatch};
use crate::build::{BuildQueue, ConfirmRequest};
use crate::deep_link::{AddRequest, InstallRequest};
use crate::toolchain::{Toolchain, Unpack};
use crate::host_install::InstallReport;
use crate::selfcheck::CheckReport;
use crate::tools::Fetch;

/// Everything the commands share. Managed by Tauri, one per app.
pub struct AppState {
    /// Set by `sign_in_cancel`; the device-flow poll loop checks it each turn.
    pub sign_in_cancel: AtomicBool,
    /// Deep links that arrived before the page asked for them (or while it was not signed in).
    pub pending_installs: Mutex<Vec<InstallRequest>>,
    /// Add links (decision 87) waiting for the Pangea plugin's service, which takes them in turn.
    pub pending_adds: Mutex<Vec<AddRequest>>,
    /// The one-at-a-time build runner and its queue.
    pub build: Arc<BuildQueue>,
    /// The confirmation the build worker is waiting for, if any, and the channel to answer it.
    pub pending_confirm: Mutex<Option<(ConfirmRequest, oneshot::Sender<bool>)>>,
    /// The token read from the keychain this launch, keyed by API base URL, so the
    /// keychain (and macOS's access prompt) is hit once per launch, not on every call.
    pub token_cache: Mutex<Option<(String, Option<String>)>>,
    /// What the last check of Apple's build tools found (`apple.rs`), and Apple's installer if
    /// the app started it.
    pub apple: Mutex<Option<AppleTools>>,
    pub apple_watch: Mutex<InstallWatch>,
    /// The app's own tools, unpacked (`toolchain.rs`); `None` in a development build.
    pub toolchain: Mutex<Option<Toolchain>>,
    /// How the unpack that starts with the app ended; `Pending` until it has.
    pub unpack: Mutex<Unpack>,
    /// While CMake or JUCE is fetched (`tools.rs`): set, so a second press waits, and the last
    /// progress, for a page that opens part way.
    pub fetching: AtomicBool,
    pub fetch_progress: Mutex<Option<Fetch>>,
    /// While the Pangea plugin is built and installed (`host_install.rs`), and how the last
    /// install ended.
    pub installing_host: AtomicBool,
    pub last_install: Mutex<Option<InstallReport>>,
    /// While the self-check runs (`selfcheck.rs`), and how the last one ended.
    pub checking: AtomicBool,
    pub last_check: Mutex<Option<CheckReport>>,
    /// When the Pangea plugin last asked the app for something from a DAW (`host.rs`), "14:02".
    pub plugin_seen: Mutex<Option<String>>,
}

impl Default for AppState {
    fn default() -> Self {
        Self {
            sign_in_cancel: AtomicBool::new(false),
            pending_installs: Mutex::new(Vec::new()),
            pending_adds: Mutex::new(Vec::new()),
            build: Arc::new(BuildQueue::default()),
            pending_confirm: Mutex::new(None),
            token_cache: Mutex::new(None),
            apple: Mutex::new(None),
            apple_watch: Mutex::new(InstallWatch::default()),
            toolchain: Mutex::new(None),
            unpack: Mutex::new(Unpack::Pending),
            fetching: AtomicBool::new(false),
            fetch_progress: Mutex::new(None),
            installing_host: AtomicBool::new(false),
            last_install: Mutex::new(None),
            checking: AtomicBool::new(false),
            last_check: Mutex::new(None),
            plugin_seen: Mutex::new(None),
        }
    }
}
