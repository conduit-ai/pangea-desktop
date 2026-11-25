//! CMake and JUCE, fetched by the app on the member's Mac the first time they are needed.
//!
//! Neither is carried in the app: CMake is 80 MB, and Pangea must not distribute JUCE (its
//! licence, and the VST3 SDK inside it). Each comes from its maker's own GitHub release at the
//! version `desktop/tools.json` pins, and is used only after its SHA-256 matches the pin:
//!
//! 1. downloaded to `~/Library/Application Support/Pangea/tools/downloads/<file>.part`, carrying
//!    on from where a failed download stopped (an HTTP `Range` request), with progress;
//! 2. checked: a file whose SHA-256 is not the pin's is deleted, and nothing is unpacked;
//! 3. unpacked with Apple's own `tar` or `unzip` into `tools/<tool>-<version>.tmp`, cleared of the
//!    quarantine flag, stamped with the checksum (`.verified`), and only then moved into place.
//!
//! A tool counts as there only with that stamp, so nothing is run that has not passed its
//! checksum. CMake's `bin` then goes on every build's `PATH` after the app's own tools, and JUCE
//! is linked into the app's copy of the shell as `shell/JUCE`. A developer's own shell folder
//! keeps its own JUCE, and a CMake 3.22 or later already on the Mac is used as it is.

use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::{Duration, Instant};

use anyhow::{anyhow, bail, Context, Result};
use serde::Serialize;
use sha2::{Digest, Sha256};

use crate::pins::{pins, Pin};
use crate::settings::ShellInfo;
use crate::toolchain::strip_quarantine;
use crate::{paths, prereqs, AppState};

const VERIFIED: &str = ".verified";
/// The event each step of a fetch is sent to the page as.
pub const FETCH_EVENT: &str = "tool-fetch";
/// The oldest CMake the shell accepts (`cmake_minimum_required` in `shell/CMakeLists.txt`).
pub const CMAKE_MINIMUM: (u32, u32) = (3, 22);
/// A download that sends nothing for this long is called stalled.
const STALL: Duration = Duration::from_secs(60);

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Tool {
    Cmake,
    Juce,
}

impl Tool {
    pub fn pin(&self) -> &'static Pin {
        match self {
            Tool::Cmake => &pins().cmake,
            Tool::Juce => &pins().juce,
        }
    }
    pub fn name(&self) -> &'static str {
        match self {
            Tool::Cmake => "CMake",
            Tool::Juce => "JUCE",
        }
    }
    fn slug(&self) -> &'static str {
        match self {
            Tool::Cmake => "cmake",
            Tool::Juce => "juce",
        }
    }
    /// Where the release comes from, for the messages ("github.com").
    fn host(&self) -> String {
        url::Url::parse(&self.pin().url).ok().and_then(|u| u.host_str().map(str::to_string)).unwrap_or_else(|| "the internet".into())
    }
}

/// What a fetch is doing, for the page.
#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct Fetch {
    pub tool: String,
    pub version: String,
    /// `download`, `check`, `unpack`, `ready` or `failed`.
    pub stage: String,
    pub received: u64,
    pub total: u64,
    pub message: String,
}

pub trait Report: Send + Sync {
    fn report(&self, f: &Fetch);
}

fn fetch_state(tool: Tool, stage: &str, received: u64, message: impl Into<String>) -> Fetch {
    Fetch {
        tool: tool.name().into(),
        version: tool.pin().version.clone(),
        stage: stage.into(),
        received,
        total: tool.pin().bytes,
        message: message.into(),
    }
}

pub fn tools_dir(support: &Path) -> PathBuf {
    support.join("tools")
}

fn installed_dir(support: &Path, tool: Tool) -> PathBuf {
    tools_dir(support).join(format!("{}-{}", tool.slug(), tool.pin().version))
}

/// The tool's folder, only when it was unpacked from a download that matched its checksum.
pub fn verified(support: &Path, tool: Tool) -> Option<PathBuf> {
    let dir = installed_dir(support, tool);
    let stamp = std::fs::read_to_string(dir.join(VERIFIED)).ok()?;
    (stamp.trim() == tool.pin().sha256).then_some(dir)
}

/// The folder holding the app's own `cmake`, when it has fetched one.
pub fn cmake_bin(support: &Path) -> Option<PathBuf> {
    let bin = verified(support, Tool::Cmake)?.join(Tool::Cmake.pin().bin_dir.as_deref()?);
    bin.join("cmake").is_file().then_some(bin)
}

/// The app's own JUCE, when it has fetched one.
pub fn juce_dir(support: &Path) -> Option<PathBuf> {
    let juce = verified(support, Tool::Juce)?.join("JUCE");
    juce.join("CMakeLists.txt").is_file().then_some(juce)
}

/// Parses "cmake version 3.30.0" into (3, 30).
pub fn parse_cmake_version(text: &str) -> Option<(u32, u32)> {
    let v = text.lines().next()?.split_whitespace().find(|w| w.chars().next().is_some_and(|c| c.is_ascii_digit()))?;
    let mut parts = v.split('.');
    Some((parts.next()?.parse().ok()?, parts.next()?.parse().ok()?))
}

/// A `cmake` the Mac already has (Homebrew's, for a developer), when it is new enough.
pub fn system_cmake() -> Option<(PathBuf, String)> {
    let found = crate::prereqs::find_tool("cmake")?;
    let out = Command::new(&found).arg("--version").env_clear().env("PATH", "/usr/bin:/bin").output().ok()?;
    let text = String::from_utf8_lossy(&out.stdout).to_string();
    let version = parse_cmake_version(&text)?;
    (version >= CMAKE_MINIMUM).then(|| (found, text.lines().next().unwrap_or("").trim().to_string()))
}

/// SHA-256 of a file, as lower-case hex.
pub fn sha256_file(path: &Path) -> Result<String> {
    let mut file = std::fs::File::open(path).with_context(|| format!("could not open {}", path.display()))?;
    let mut hasher = Sha256::new();
    let mut buf = vec![0u8; 1 << 20];
    loop {
        let n = file.read(&mut buf)?;
        if n == 0 {
            break;
        }
        hasher.update(&buf[..n]);
    }
    Ok(hasher.finalize().iter().map(|b| format!("{b:02x}")).collect())
}

/// The client every fetch uses: a connection must open within 20 seconds.
/// HTTPS only, redirects included: a network that sends the app to a sign-in page over plain
/// HTTP gets one clear message, not a page saved as a download.
pub fn client() -> Result<reqwest::Client> {
    client_with(true)
}

/// `client`, with plain HTTP allowed for the tests' local server.
pub fn client_with(https_only: bool) -> Result<reqwest::Client> {
    reqwest::Client::builder()
        .connect_timeout(Duration::from_secs(20))
        .https_only(https_only)
        .user_agent(format!("pangea-desktop/{}", env!("CARGO_PKG_VERSION")))
        .build()
        .context("could not make an HTTP client")
}

/// What came back is not the release: a network's sign-in page, most often.
fn not_the_download(tool: Tool) -> anyhow::Error {
    anyhow!(
        "What came back from {} was not the download of {}. A network that asks you to sign in, as in a hotel or a café, does this: open a web page, sign in to the network, then press Try again.",
        tool.host(),
        tool.name()
    )
}

/// One sentence for a failed request.
fn network_problem(tool: Tool, e: &reqwest::Error) -> anyhow::Error {
    if e.is_redirect() || e.is_builder() {
        // A redirect away from HTTPS, which https_only refuses: a network's sign-in page.
        not_the_download(tool)
    } else if e.is_connect() || e.is_timeout() {
        anyhow!(
            "Pangea could not reach {} to fetch {}. Check your internet connection, then press Try again.",
            tool.host(),
            tool.name()
        )
    } else {
        anyhow!("The download of {} stopped ({e}). Press Try again: it carries on from where it stopped.", tool.name())
    }
}

/// Downloads `url` into `part`, carrying on from what `part` already holds when the server
/// allows it. `total` is the size the pin names.
async fn download(client: &reqwest::Client, tool: Tool, url: &str, total: u64, part: &Path, report: &dyn Report) -> Result<()> {
    let mut have = std::fs::metadata(part).map(|m| m.len()).unwrap_or(0);
    if have > total {
        std::fs::remove_file(part)?;
        have = 0;
    }
    if have == total {
        return Ok(());
    }
    let mut request = client.get(url);
    if have > 0 {
        request = request.header(reqwest::header::RANGE, format!("bytes={have}-"));
    }
    let mut response = request.send().await.map_err(|e| network_problem(tool, &e))?;
    let status = response.status().as_u16();
    if status == 200 {
        // The server sent the whole file: start again from nothing.
        have = 0;
    }
    // An answer of another length than the pin's is not the release (a sign-in page, an error
    // page): nothing of it is kept.
    if matches!(status, 200 | 206) && response.content_length().is_some_and(|len| len != total - have) {
        let _ = std::fs::remove_file(part);
        return Err(not_the_download(tool));
    }
    let mut file = match status {
        206 => std::fs::OpenOptions::new().append(true).open(part)?,
        200 => std::fs::File::create(part)?,
        code => bail!("{} answered {code} for {}. Press Try again in a few minutes.", tool.host(), tool.name()),
    };
    report.report(&fetch_state(tool, "download", have, format!("Downloading {}", tool.name())));
    let mut last = Instant::now();
    loop {
        let chunk = match tokio::time::timeout(STALL, response.chunk()).await {
            Err(_) => bail!(
                "The download of {} stalled. Check your internet connection, then press Try again: it carries on from where it stopped.",
                tool.name()
            ),
            Ok(Err(e)) => return Err(network_problem(tool, &e)),
            Ok(Ok(None)) => break,
            Ok(Ok(Some(c))) => c,
        };
        if have + chunk.len() as u64 > total {
            // More than the release holds: an answer with no length that is not the release.
            drop(file);
            let _ = std::fs::remove_file(part);
            return Err(not_the_download(tool));
        }
        file.write_all(&chunk).with_context(|| format!("could not write {}", part.display()))?;
        have += chunk.len() as u64;
        if last.elapsed() >= Duration::from_millis(250) {
            last = Instant::now();
            report.report(&fetch_state(tool, "download", have, format!("Downloading {}", tool.name())));
        }
    }
    file.flush()?;
    if have != total {
        bail!(
            "The download of {} stopped at {} of {} MB. Press Try again: it carries on from where it stopped.",
            tool.name(),
            have / 1_000_000,
            total / 1_000_000
        );
    }
    Ok(())
}

/// The folder a tool is unpacked into before it is moved into place: `<tool>-<version>.tmp`.
/// (`with_extension` would take the version's last number for an extension: `cmake-3.31.tmp`.)
pub fn tmp_dir_for(dir: &Path) -> PathBuf {
    let name = dir.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default();
    dir.with_file_name(format!("{name}.tmp"))
}

/// Unpacks a checked archive with Apple's own tools. JUCE's two demonstration apps are left out.
fn unpack_archive(tool: Tool, archive: &Path, into: &Path) -> Result<()> {
    std::fs::create_dir_all(into)?;
    let out = match tool {
        Tool::Cmake => Command::new("/usr/bin/tar")
            .arg("-xzf")
            .arg(archive)
            .arg("-C")
            .arg(into)
            .args(["--strip-components", "1"])
            .output(),
        Tool::Juce => Command::new("/usr/bin/unzip")
            .arg("-q")
            .arg(archive)
            .arg("-d")
            .arg(into)
            .args(["-x", "JUCE/DemoRunner.app/*", "JUCE/Projucer.app/*"])
            .output(),
    }
    .context("could not run tar or unzip")?;
    if !out.status.success() {
        bail!("could not unpack {}: {}", tool.name(), String::from_utf8_lossy(&out.stderr).trim());
    }
    Ok(())
}

/// What must be there once a tool is unpacked.
fn check_unpacked(tool: Tool, dir: &Path) -> Result<()> {
    let needed = match tool {
        Tool::Cmake => dir.join(Tool::Cmake.pin().bin_dir.as_deref().unwrap_or("bin")).join("cmake"),
        Tool::Juce => dir.join("JUCE").join("CMakeLists.txt"),
    };
    if !needed.is_file() {
        bail!("{} was unpacked but has no {}", tool.name(), needed.display());
    }
    Ok(())
}

/// Fetches, checks and unpacks a tool unless it is already there. Returns its folder.
pub async fn fetch(client: &reqwest::Client, tool: Tool, support: &Path, report: &dyn Report) -> Result<PathBuf> {
    if !tool.pin().url.starts_with("https://") {
        bail!("{} is pinned to an address that is not HTTPS", tool.name());
    }
    fetch_from(client, tool, &tool.pin().url, tool.pin(), support, report).await
}

/// `fetch`, from `url` against `pin` (tests give a local server and a pin of their own).
pub async fn fetch_from(client: &reqwest::Client, tool: Tool, url: &str, pin: &Pin, support: &Path, report: &dyn Report) -> Result<PathBuf> {
    let dir = tools_dir(support).join(format!("{}-{}", tool.slug(), pin.version));
    let stamped = std::fs::read_to_string(dir.join(VERIFIED)).map(|s| s.trim() == pin.sha256).unwrap_or(false);
    if stamped {
        report.report(&fetch_state(tool, "ready", pin.bytes, format!("{} {} is ready", tool.name(), pin.version)));
        return Ok(dir);
    }
    let result = fetch_new(client, tool, url, pin, support, &dir, report).await;
    if let Err(e) = &result {
        report.report(&fetch_state(tool, "failed", 0, format!("{e:#}")));
    }
    result
}

async fn fetch_new(client: &reqwest::Client, tool: Tool, url: &str, pin: &Pin, support: &Path, dir: &Path, report: &dyn Report) -> Result<PathBuf> {
    let downloads = tools_dir(support).join("downloads");
    std::fs::create_dir_all(&downloads).with_context(|| format!("could not create {}", downloads.display()))?;
    let name = url.rsplit('/').next().unwrap_or("download");
    let part = downloads.join(format!("{name}.part"));
    download(client, tool, url, pin.bytes, &part, report).await?;

    report.report(&fetch_state(tool, "check", pin.bytes, format!("Checking {}", tool.name())));
    let sum = sha256_file(&part)?;
    if sum != pin.sha256 {
        let _ = std::fs::remove_file(&part);
        bail!(
            "The download of {} did not match its checksum, so Pangea deleted it and used nothing from it. Press Try again to download it again.",
            tool.name()
        );
    }
    let archive = downloads.join(name);
    std::fs::rename(&part, &archive)?;

    report.report(&fetch_state(tool, "unpack", pin.bytes, format!("Unpacking {}", tool.name())));
    let tmp = tmp_dir_for(dir);
    let _ = std::fs::remove_dir_all(&tmp);
    if let Err(e) = unpack_archive(tool, &archive, &tmp).and_then(|()| check_unpacked(tool, &tmp)) {
        let _ = std::fs::remove_dir_all(&tmp);
        let _ = std::fs::remove_file(&archive);
        return Err(e);
    }
    strip_quarantine(&tmp)?;
    std::fs::write(tmp.join(VERIFIED), format!("{}\n", pin.sha256))?;
    if dir.exists() {
        std::fs::remove_dir_all(dir)?;
    }
    std::fs::rename(&tmp, dir).with_context(|| format!("could not move {} into place", dir.display()))?;
    let _ = std::fs::remove_file(&archive);
    report.report(&fetch_state(tool, "ready", pin.bytes, format!("{} {} is ready", tool.name(), pin.version)));
    Ok(dir.to_path_buf())
}

/// What builds still need fetched, given the shell they use.
#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct Needs {
    /// No CMake of the app's own and none of 3.22 or later on the Mac.
    pub cmake: bool,
    /// The app's own shell has no JUCE yet.
    pub juce: bool,
    /// A developer's own shell folder without JUCE: the app does not write into it.
    pub juce_in_own_shell_missing: bool,
}

impl Needs {
    pub fn nothing(&self) -> bool {
        !self.cmake && !self.juce
    }
}

pub fn needs(support: &Path, shell: &ShellInfo) -> Needs {
    let cmake = cmake_bin(support).is_none() && system_cmake().is_none();
    let has_juce = !shell.dir.is_empty() && Path::new(&shell.dir).join("JUCE").join("CMakeLists.txt").is_file();
    Needs {
        cmake,
        juce: shell.source == "app" && !has_juce,
        juce_in_own_shell_missing: shell.source == "own" && !has_juce,
    }
}

/// Puts the app's own tools on every build's `PATH`: the Faust and Ninja it carries first, then
/// its CMake once fetched. Links a fetched JUCE into the app's own shell, which a new version of
/// the app unpacks without it. Called at start and after each fetch.
pub fn apply(state: &AppState, shell: &ShellInfo) -> Result<()> {
    let support = paths::support_dir()?;
    let mut dirs = Vec::new();
    if let Some(t) = state.toolchain.lock().unwrap().clone() {
        dirs.push(t.bin_dir());
    }
    if let Some(bin) = cmake_bin(&support) {
        dirs.push(bin);
    }
    prereqs::set_tool_dirs(dirs);
    if shell.source == "app" {
        if let Some(juce) = juce_dir(&support) {
            link_juce(Path::new(&shell.dir), &juce)?;
        }
    }
    Ok(())
}

/// Fetches what builds still need, one tool after the other, then applies it.
pub async fn ensure(state: &AppState, shell: &ShellInfo, report: &dyn Report) -> Result<Needs> {
    let support = paths::support_dir()?;
    apply(state, shell)?;
    let need = needs(&support, shell);
    if !need.nothing() {
        let client = client()?;
        if need.cmake {
            fetch(&client, Tool::Cmake, &support, report).await?;
        }
        if need.juce {
            fetch(&client, Tool::Juce, &support, report).await?;
        }
        apply(state, shell)?;
    }
    Ok(needs(&support, shell))
}

/// Links the app's JUCE into the app's copy of the shell as `shell/JUCE`. A real folder there
/// (a developer's own JUCE) is left alone.
pub fn link_juce(shell_dir: &Path, juce: &Path) -> Result<()> {
    let link = shell_dir.join("JUCE");
    match std::fs::symlink_metadata(&link) {
        Ok(m) if m.file_type().is_symlink() => {
            if std::fs::read_link(&link).ok().as_deref() == Some(juce) {
                return Ok(());
            }
            std::fs::remove_file(&link)?;
        }
        Ok(_) => return Ok(()),
        Err(_) => {}
    }
    std::os::unix::fs::symlink(juce, &link).with_context(|| format!("could not link JUCE into {}", shell_dir.display()))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU32, AtomicUsize, Ordering};
    use std::sync::{Arc, Mutex};
    use tokio::io::{AsyncReadExt, AsyncWriteExt};

    struct TempDir(PathBuf);
    impl TempDir {
        fn new(tag: &str) -> TempDir {
            static N: AtomicU32 = AtomicU32::new(0);
            let p = std::env::temp_dir().join(format!("pangea-tools-test-{}-{}-{tag}", std::process::id(), N.fetch_add(1, Ordering::SeqCst)));
            let _ = std::fs::remove_dir_all(&p);
            std::fs::create_dir_all(&p).unwrap();
            TempDir(p)
        }
    }
    impl Drop for TempDir {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.0);
        }
    }

    #[derive(Default)]
    struct Seen(Mutex<Vec<Fetch>>);
    impl Report for Seen {
        fn report(&self, f: &Fetch) {
            self.0.lock().unwrap().push(f.clone());
        }
    }
    impl Seen {
        fn stages(&self) -> Vec<String> {
            let mut s: Vec<String> = self.0.lock().unwrap().iter().map(|f| f.stage.clone()).collect();
            s.dedup();
            s
        }
    }

    /// A CMake release in miniature: `<top>/CMake.app/Contents/bin/cmake`, as a tar.gz.
    fn tiny_cmake(dir: &Path) -> Vec<u8> {
        let top = dir.join("src/cmake-tiny");
        std::fs::create_dir_all(top.join("CMake.app/Contents/bin")).unwrap();
        std::fs::write(top.join("CMake.app/Contents/bin/cmake"), "#!/bin/sh\necho 'cmake version 3.31.12'\n").unwrap();
        // Enough bytes that a download can be cut part way.
        let filler: String = (0..40_000).map(|i| char::from(b'a' + (i * 7 % 26) as u8)).collect();
        std::fs::write(top.join("filler.txt"), filler).unwrap();
        let out = dir.join("cmake-tiny.tar.gz");
        let ok = Command::new("/usr/bin/tar").arg("-czf").arg(&out).arg("-C").arg(dir.join("src")).arg("cmake-tiny").status().unwrap();
        assert!(ok.success());
        std::fs::read(out).unwrap()
    }

    fn pin_for(bytes: &[u8], sha: Option<&str>) -> Pin {
        let mut h = Sha256::new();
        h.update(bytes);
        Pin {
            version: "3.31.12".into(),
            url: String::new(),
            sha256: sha.map(str::to_string).unwrap_or_else(|| h.finalize().iter().map(|b| format!("{b:02x}")).collect()),
            bytes: bytes.len() as u64,
            licence: "BSD-3-Clause".into(),
            bin_dir: Some("CMake.app/Contents/bin".into()),
        }
    }

    /// A small HTTP server: answers `Range` with 206 and the rest of the body. While `cut` is
    /// set, a whole-body answer stops after `cut` bytes and drops the connection, as a dropped
    /// connection does. Counts the requests and keeps the `Range` headers it saw.
    async fn serve(body: Vec<u8>, cut: Arc<AtomicUsize>) -> (String, Arc<Mutex<Vec<String>>>) {
        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let url = format!("http://{}/cmake-tiny.tar.gz", listener.local_addr().unwrap());
        let ranges = Arc::new(Mutex::new(Vec::new()));
        let seen = ranges.clone();
        tokio::spawn(async move {
            loop {
                let Ok((mut sock, _)) = listener.accept().await else { return };
                let mut buf = vec![0u8; 4096];
                let n = sock.read(&mut buf).await.unwrap_or(0);
                let request = String::from_utf8_lossy(&buf[..n]).to_string();
                let range = request
                    .lines()
                    .find_map(|l| l.to_ascii_lowercase().strip_prefix("range: bytes=").map(|r| r.trim_end_matches('-').to_string()));
                seen.lock().unwrap().push(range.clone().unwrap_or_default());
                let start: usize = range.as_deref().and_then(|r| r.parse().ok()).unwrap_or(0);
                let rest = &body[start..];
                let head = if start > 0 {
                    format!(
                        "HTTP/1.1 206 Partial Content\r\nContent-Length: {}\r\nContent-Range: bytes {}-{}/{}\r\nConnection: close\r\n\r\n",
                        rest.len(),
                        start,
                        body.len() - 1,
                        body.len()
                    )
                } else {
                    format!("HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n", rest.len())
                };
                let _ = sock.write_all(head.as_bytes()).await;
                let limit = cut.load(Ordering::SeqCst);
                if start == 0 && limit > 0 {
                    let _ = sock.write_all(&rest[..limit]).await;
                    let _ = sock.shutdown().await;
                    continue;
                }
                let _ = sock.write_all(rest).await;
                let _ = sock.shutdown().await;
            }
        });
        (url, ranges)
    }

    #[tokio::test]
    async fn a_failed_download_carries_on_and_is_checked_before_it_is_unpacked() {
        let work = TempDir::new("resume");
        let body = tiny_cmake(&work.0);
        let cut = Arc::new(AtomicUsize::new(body.len() / 3));
        let (url, ranges) = serve(body.clone(), cut.clone()).await;
        let support = work.0.join("support");
        let pin = pin_for(&body, None);
        let client = client_with(false).unwrap();

        // The connection drops a third of the way: the part stays, nothing is unpacked.
        let seen = Seen::default();
        let err = fetch_from(&client, Tool::Cmake, &url, &pin, &support, &seen).await.unwrap_err().to_string();
        assert!(err.contains("carries on from where it stopped"), "{err}");
        let part = tools_dir(&support).join("downloads/cmake-tiny.tar.gz.part");
        assert_eq!(std::fs::metadata(&part).unwrap().len() as usize, body.len() / 3);
        assert!(!tools_dir(&support).join("cmake-3.31.12").exists());

        // Try again: it asks for the rest only, checks the whole and unpacks it.
        cut.store(0, Ordering::SeqCst);
        let seen = Seen::default();
        let dir = fetch_from(&client, Tool::Cmake, &url, &pin, &support, &seen).await.unwrap();
        assert_eq!(ranges.lock().unwrap().last().map(String::as_str), Some(format!("{}", body.len() / 3).as_str()));
        assert!(dir.join("CMake.app/Contents/bin/cmake").is_file());
        assert_eq!(std::fs::read_to_string(dir.join(VERIFIED)).unwrap().trim(), pin.sha256);
        assert!(!part.exists(), "the part is gone once checked");
        assert!(!crate::toolchain::has_quarantine(&dir.join("CMake.app/Contents/bin/cmake")));
        assert_eq!(seen.stages(), vec!["download", "check", "unpack", "ready"]);

        // A third time it is there already: no request.
        let before = ranges.lock().unwrap().len();
        fetch_from(&client, Tool::Cmake, &url, &pin, &support, &Seen::default()).await.unwrap();
        assert_eq!(ranges.lock().unwrap().len(), before);
    }

    #[tokio::test]
    async fn a_download_that_does_not_match_its_checksum_is_deleted_and_never_unpacked() {
        let work = TempDir::new("checksum");
        let body = tiny_cmake(&work.0);
        let (url, _) = serve(body.clone(), Arc::new(AtomicUsize::new(0))).await;
        let support = work.0.join("support");
        let wrong = pin_for(&body, Some(&"0".repeat(64)));
        let seen = Seen::default();
        let err = fetch_from(&client_with(false).unwrap(), Tool::Cmake, &url, &wrong, &support, &seen).await.unwrap_err().to_string();
        assert!(err.contains("did not match its checksum"), "{err}");
        assert!(!tools_dir(&support).join("downloads/cmake-tiny.tar.gz.part").exists());
        assert!(!tools_dir(&support).join("cmake-3.31.12").exists());
        assert!(!tools_dir(&support).join("cmake-3.31.12.tmp").exists());
        assert!(!tools_dir(&support).join("cmake-3.31.tmp").exists());
        assert_eq!(tmp_dir_for(&tools_dir(&support).join("cmake-3.31.12")), tools_dir(&support).join("cmake-3.31.12.tmp"));
        assert_eq!(tmp_dir_for(Path::new("/x/juce-8.0.10")), PathBuf::from("/x/juce-8.0.10.tmp"));
        assert_eq!(seen.stages().last().map(String::as_str), Some("failed"));
    }

    /// A server that answers every request with the same bytes, then closes.
    async fn serve_raw(answer: Vec<u8>) -> String {
        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let url = format!("http://{}/cmake-tiny.tar.gz", listener.local_addr().unwrap());
        tokio::spawn(async move {
            loop {
                let Ok((mut sock, _)) = listener.accept().await else { return };
                let mut buf = vec![0u8; 4096];
                let _ = sock.read(&mut buf).await;
                let _ = sock.write_all(&answer).await;
                let _ = sock.shutdown().await;
            }
        });
        url
    }

    /// A network's sign-in page in place of the release: one clear sentence, nothing kept.
    #[tokio::test]
    async fn an_answer_that_is_not_the_release_is_refused_once_and_plainly() {
        let work = TempDir::new("portal");
        let body = tiny_cmake(&work.0);
        let pin = pin_for(&body, None);
        let support = work.0.join("support");
        let part = tools_dir(&support).join("downloads/cmake-tiny.tar.gz.part");

        // A page of its own length.
        let page = b"<html><body>Sign in to the hotel's Wi-Fi</body></html>";
        let mut answer = format!("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: {}\r\nConnection: close\r\n\r\n", page.len()).into_bytes();
        answer.extend_from_slice(page);
        let url = serve_raw(answer).await;
        let seen = Seen::default();
        let err = fetch_from(&client_with(false).unwrap(), Tool::Cmake, &url, &pin, &support, &seen).await.unwrap_err().to_string();
        assert!(err.starts_with("What came back from"), "{err}");
        assert!(err.contains("sign in to the network"), "{err}");
        assert!(!part.exists(), "nothing of the page is kept");
        assert_eq!(seen.0.lock().unwrap().iter().filter(|f| f.stage == "failed").count(), 1, "one message");

        // No length, and more than the release holds: stopped, deleted.
        let mut long = b"HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n".to_vec();
        long.extend(std::iter::repeat_n(b'x', body.len() + 4096));
        let url = serve_raw(long).await;
        let err = fetch_from(&client_with(false).unwrap(), Tool::Cmake, &url, &pin, &support, &Seen::default()).await.unwrap_err().to_string();
        assert!(err.starts_with("What came back from"), "{err}");
        assert!(!part.exists());

        // The app's own client speaks HTTPS only: plain HTTP is refused before a byte is kept.
        let err = fetch_from(&client().unwrap(), Tool::Cmake, &url, &pin, &support, &Seen::default()).await.unwrap_err().to_string();
        assert!(err.starts_with("What came back from") || err.starts_with("Pangea could not reach"), "{err}");
        assert!(!part.exists());
        assert!(!tools_dir(&support).join("cmake-3.31.12").exists());
    }

    #[tokio::test]
    async fn offline_says_so_in_plain_words() {
        let work = TempDir::new("offline");
        // Nothing listens on port 9 here: the connection is refused, as when the Mac is offline.
        let pin = pin_for(b"x", None);
        let err = fetch_from(&client_with(false).unwrap(), Tool::Juce, "http://127.0.0.1:9/juce.zip", &pin, &work.0, &Seen::default())
            .await
            .unwrap_err()
            .to_string();
        assert!(err.starts_with("Pangea could not reach"), "{err}");
        assert!(err.contains("Check your internet connection, then press Try again."), "{err}");
    }

    #[test]
    fn juce_is_linked_into_the_apps_shell_but_a_developers_own_is_left_alone() {
        let work = TempDir::new("link");
        let shell = work.0.join("shell");
        let juce = work.0.join("tools/juce-8.0.10/JUCE");
        std::fs::create_dir_all(&shell).unwrap();
        std::fs::create_dir_all(&juce).unwrap();
        link_juce(&shell, &juce).unwrap();
        assert_eq!(std::fs::read_link(shell.join("JUCE")).unwrap(), juce);
        link_juce(&shell, &juce).unwrap();

        let own = work.0.join("own-shell");
        std::fs::create_dir_all(own.join("JUCE")).unwrap();
        link_juce(&own, &juce).unwrap();
        assert!(!std::fs::symlink_metadata(own.join("JUCE")).unwrap().file_type().is_symlink());
    }

    #[test]
    fn cmake_versions_are_read() {
        assert_eq!(parse_cmake_version("cmake version 3.30.0\n\nCMake suite maintained"), Some((3, 30)));
        assert_eq!(parse_cmake_version("cmake version 4.5.0-rc1"), Some((4, 5)));
        assert!(parse_cmake_version("cmake version 3.21.1").unwrap() < CMAKE_MINIMUM);
        assert_eq!(parse_cmake_version("nothing"), None);
    }

}
