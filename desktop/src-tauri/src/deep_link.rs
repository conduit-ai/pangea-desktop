//! `pangea://install?project=<id>&version=<n>`: the web app's "Install AU / VST3" button.
//! `pangea://add?project=<id>&version=<n>`: its "Add to Pangea plugin" and "Update in Pangea
//! plugin" button (decision 87), which has the Pangea plugin's service get that version ready
//! as a module (host.rs). The scheme is registered in the bundle's Info.plist by the
//! deep-link plugin (tauri.conf.json > plugins > deep-link > desktop > schemes), so it works
//! only from the bundled app, not from `tauri dev`.

use serde::{Deserialize, Serialize};
use tauri::{App, AppHandle, Emitter, Manager, Runtime};
use tauri_plugin_deep_link::DeepLinkExt;
use url::Url;

use crate::AppState;

pub const EVENT: &str = "install-request";

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct InstallRequest {
    pub project: String,
    pub version: Option<u32>,
    pub url: String,
}

/// A version to get ready in the Pangea plugin: the build view's main button (decision 87).
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AddRequest {
    pub project: String,
    pub version: u32,
}

/// The link's action: `pangea://install?...` parses with host "install"; `pangea:///install?...`
/// with path "/install".
fn action(url: &Url) -> Option<String> {
    if url.scheme() != "pangea" {
        return None;
    }
    Some(url.host_str().map(str::to_string).unwrap_or_else(|| url.path().trim_matches('/').to_string()))
}

pub fn parse(url: &Url) -> Option<InstallRequest> {
    if action(url)? != "install" {
        return None;
    }
    let (project, version) = project_and_version(url)?;
    Some(InstallRequest { project, version, url: url.to_string() })
}

/// `pangea://add?project=<id>&version=<n>`: the version is needed, since the button sends the
/// plugin's current version as the site sees it.
pub fn parse_add(url: &Url) -> Option<AddRequest> {
    if action(url)? != "add" {
        return None;
    }
    let (project, version) = project_and_version(url)?;
    Some(AddRequest { project, version: version? })
}

/// The project and the version a link names, checked: a link is untrusted input.
fn project_and_version(url: &Url) -> Option<(String, Option<u32>)> {
    let mut project = None;
    let mut version = None;
    let mut bad_version = false;
    for (k, v) in url.query_pairs() {
        match k.as_ref() {
            "project" | "projectId" => project = Some(v.to_string()),
            "version" | "number" => match v.trim().parse::<u32>() {
                Ok(n) if crate::api::validate_version(n).is_ok() => version = Some(n),
                _ => bad_version = true,
            },
            _ => {}
        }
    }
    // A link is untrusted input: the project id must be a UUID and the version, if
    // given, a positive integer, or the link is ignored before anything is requested.
    let project = project?;
    if crate::api::validate_project_id(&project).is_err() || bad_version {
        return None;
    }
    Some((project, version))
}

pub fn install(app: &mut App) {
    let handle = app.handle().clone();
    app.deep_link().on_open_url(move |event| {
        for url in event.urls() {
            handle_url(&handle, &url);
        }
    });
    // The app may have been launched by the link itself.
    if let Ok(Some(urls)) = app.deep_link().get_current() {
        let handle = app.handle().clone();
        for url in urls {
            handle_url(&handle, &url);
        }
    }
}

fn handle_url<R: Runtime>(app: &AppHandle<R>, url: &Url) {
    if let Some(add) = parse_add(url) {
        // The Pangea plugin's service takes it on its next turn (host.rs).
        if let Ok(mut pending) = app.state::<AppState>().pending_adds.lock() {
            pending.push(add);
        }
        bring_forward(app);
        return;
    }
    let Some(req) = parse(url) else {
        eprintln!("ignored deep link: {url}");
        return;
    };
    bring_forward(app);
    if let Ok(mut pending) = app.state::<AppState>().pending_installs.lock() {
        pending.push(req.clone());
    }
    let _ = app.emit(EVENT, &req);
}

pub fn bring_forward<R: Runtime>(app: &AppHandle<R>) {
    if let Some(w) = app.get_webview_window("main") {
        let _ = w.show();
        let _ = w.unminimize();
        let _ = w.set_focus();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const ID: &str = "3f2a1c4e-9b7d-4e6a-8c1f-0a2b3c4d5e6f";

    #[test]
    fn parses_install_links() {
        let r = parse(&Url::parse(&format!("pangea://install?project={ID}&version=3")).unwrap()).unwrap();
        assert_eq!(r.project, ID);
        assert_eq!(r.version, Some(3));
        let r = parse(&Url::parse(&format!("pangea:///install?project={ID}")).unwrap()).unwrap();
        assert_eq!(r.project, ID);
        assert_eq!(r.version, None);
        assert!(parse(&Url::parse(&format!("pangea://other?project={ID}")).unwrap()).is_none());
        assert!(parse(&Url::parse(&format!("https://example.com/install?project={ID}")).unwrap()).is_none());
    }

    #[test]
    fn hostile_links_are_ignored() {
        // a traversal attempt in the project id
        assert!(parse(&Url::parse("pangea://install?project=../me&version=1").unwrap()).is_none());
        assert!(parse(&Url::parse("pangea://install?project=..%2Fme&version=1").unwrap()).is_none());
        assert!(parse(&Url::parse("pangea://install?project=p1&version=1").unwrap()).is_none());
        // a bad version
        assert!(parse(&Url::parse(&format!("pangea://install?project={ID}&version=0")).unwrap()).is_none());
        assert!(parse(&Url::parse(&format!("pangea://install?project={ID}&version=-1")).unwrap()).is_none());
        assert!(parse(&Url::parse(&format!("pangea://install?project={ID}&version=abc")).unwrap()).is_none());
        assert!(parse(&Url::parse(&format!("pangea://install?project={ID}&version=1/../2")).unwrap()).is_none());
    }

    #[test]
    fn parses_add_links_and_needs_a_version() {
        let r = parse_add(&Url::parse(&format!("pangea://add?project={ID}&version=7")).unwrap()).unwrap();
        assert_eq!(r, AddRequest { project: ID.to_string(), version: 7 });
        let r = parse_add(&Url::parse(&format!("pangea:///add?project={ID}&version=2")).unwrap()).unwrap();
        assert_eq!(r.version, 2);
        // The button always names the version it sends.
        assert!(parse_add(&Url::parse(&format!("pangea://add?project={ID}")).unwrap()).is_none());
        // An add link is not an install link, nor the other way round.
        assert!(parse(&Url::parse(&format!("pangea://add?project={ID}&version=7")).unwrap()).is_none());
        assert!(parse_add(&Url::parse(&format!("pangea://install?project={ID}&version=7")).unwrap()).is_none());
        // The same checks as an install link.
        assert!(parse_add(&Url::parse("pangea://add?project=../me&version=1").unwrap()).is_none());
        assert!(parse_add(&Url::parse(&format!("pangea://add?project={ID}&version=0")).unwrap()).is_none());
        assert!(parse_add(&Url::parse(&format!("pangea://add?project={ID}&version=abc")).unwrap()).is_none());
        assert!(parse_add(&Url::parse(&format!("https://example.com/add?project={ID}&version=1")).unwrap()).is_none());
    }
}
