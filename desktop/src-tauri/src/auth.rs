//! Sign in with the OAuth device flow (RFC 8628) against Better Auth's
//! deviceAuthorization plugin, and keep the token in the keychain.

use std::sync::atomic::Ordering;
use std::time::{Duration, Instant};

use anyhow::{bail, Result};
use serde::Serialize;
use tauri::{AppHandle, Manager, Runtime};
use tauri_plugin_opener::OpenerExt;

use crate::api::{self, Api, DeviceCodeResponse, TokenPoll};
use crate::{keychain, settings, AppState};

#[derive(Debug, Clone, Serialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct AuthStatus {
    pub signed_in: bool,
    pub email: Option<String>,
    pub name: Option<String>,
    pub api_base_url: String,
    /// Set when the token exists but `GET /api/desktop/me` could not be reached.
    pub error: Option<String>,
}

/// The token for a base URL: from this launch's cache, else from the keychain (which may
/// make macOS ask once whether this binary may read the item).
fn token_for<R: Runtime>(app: &AppHandle<R>, api_base_url: &str) -> Result<Option<String>> {
    let state = app.state::<AppState>();
    if let Some((base, token)) = state.token_cache.lock().unwrap().as_ref() {
        if base == api_base_url {
            return Ok(token.clone());
        }
    }
    let token = match keychain::load_token(api_base_url) {
        Ok(t) => t,
        Err(e) if keychain::is_access_refused(&e) => {
            // A stale item from another build of the app. Removing it needs no access to
            // the secret; the user signs in again and the new item belongs to this binary.
            let _ = keychain::clear_token(api_base_url);
            *state.token_cache.lock().unwrap() = Some((api_base_url.to_string(), None));
            return Err(e);
        }
        Err(e) => return Err(e),
    };
    *state.token_cache.lock().unwrap() = Some((api_base_url.to_string(), token.clone()));
    Ok(token)
}

fn remember_token<R: Runtime>(app: &AppHandle<R>, api_base_url: &str, token: Option<String>) {
    *app.state::<AppState>().token_cache.lock().unwrap() = Some((api_base_url.to_string(), token));
}

/// Opens an API client with whatever token the keychain holds for the configured base URL.
pub fn client<R: Runtime>(app: &AppHandle<R>) -> Result<(Api, bool)> {
    let s = settings::load(app)?;
    let token = token_for(app, &s.api_base_url)?;
    let has_token = token.is_some();
    Ok((Api::new(&s.api_base_url, token)?, has_token))
}

/// The API base URL and a fingerprint of the token held for it (`None` when signed out),
/// for noticing a sign-in or sign-out. It reads this launch's cache, so asking often costs
/// nothing and does not touch the keychain. The token itself stays in this module.
pub fn session<R: Runtime>(app: &AppHandle<R>) -> Result<(String, Option<u64>)> {
    use std::hash::{Hash, Hasher};
    let s = settings::load(app)?;
    let fingerprint = token_for(app, &s.api_base_url)?.map(|t| {
        let mut h = std::collections::hash_map::DefaultHasher::new();
        t.hash(&mut h);
        h.finish()
    });
    Ok((s.api_base_url, fingerprint))
}

/// Like `client` but fails when there is no token.
pub fn signed_in_client<R: Runtime>(app: &AppHandle<R>) -> Result<Api> {
    let (api, has_token) = client(app)?;
    if !has_token {
        bail!("not signed in");
    }
    Ok(api)
}

pub async fn status<R: Runtime>(app: &AppHandle<R>) -> Result<AuthStatus> {
    let s = settings::load(app)?;
    let (api, has_token) = match client(app) {
        Ok(c) => c,
        Err(e) if keychain::is_access_refused(&e) => {
            return Ok(AuthStatus {
                api_base_url: s.api_base_url,
                error: Some(
                    "The keychain did not let this build of Pangea read the saved token (this happens after the app is rebuilt). Sign in again."
                        .into(),
                ),
                ..Default::default()
            })
        }
        Err(e) => return Err(e),
    };
    if !has_token {
        return Ok(AuthStatus { api_base_url: s.api_base_url, ..Default::default() });
    }
    match api.me().await {
        Ok(me) => Ok(AuthStatus {
            signed_in: true,
            email: Some(me.email),
            name: me.name,
            api_base_url: s.api_base_url,
            error: None,
        }),
        Err(e) if api::is_unauthorised(&e) => {
            keychain::clear_token(&s.api_base_url)?;
            remember_token(app, &s.api_base_url, None);
            Ok(AuthStatus {
                api_base_url: s.api_base_url,
                error: Some("Your session has expired. Sign in again.".into()),
                ..Default::default()
            })
        }
        Err(e) => Ok(AuthStatus {
            signed_in: true,
            api_base_url: s.api_base_url,
            error: Some(format!("{e:#}")),
            ..Default::default()
        }),
    }
}

/// Step 1: ask for a device code and open the verification page in the browser.
pub async fn start<R: Runtime>(app: &AppHandle<R>) -> Result<DeviceCodeResponse> {
    let (api, _) = client(app)?;
    let code = api.device_code().await?;
    app.state::<AppState>().sign_in_cancel.store(false, Ordering::SeqCst);
    // Best effort: if the browser does not open, the page shows the URL to visit.
    let _ = app.opener().open_url(code.verification_uri_complete.clone(), None::<&str>);
    Ok(code)
}

/// Step 2: poll until the user approves, denies, the code expires or the page cancels.
pub async fn wait<R: Runtime>(
    app: &AppHandle<R>,
    device_code: &str,
    interval: u64,
    expires_in: u64,
) -> Result<AuthStatus> {
    let s = settings::load(app)?;
    let api = Api::new(&s.api_base_url, None)?;
    let state = app.state::<AppState>();
    let deadline = Instant::now() + Duration::from_secs(expires_in.max(30));
    let mut wait = Duration::from_secs(interval.max(1));
    loop {
        if state.sign_in_cancel.load(Ordering::SeqCst) {
            bail!("Sign-in cancelled.");
        }
        if Instant::now() > deadline {
            bail!("The code expired before it was approved. Start again.");
        }
        tokio::time::sleep(wait + Duration::from_millis(250)).await;
        if state.sign_in_cancel.load(Ordering::SeqCst) {
            bail!("Sign-in cancelled.");
        }
        match api.device_token(device_code).await? {
            TokenPoll::Pending => continue,
            TokenPoll::SlowDown => {
                // RFC 8628 5.2: add five seconds to the interval.
                wait += Duration::from_secs(5);
                continue;
            }
            TokenPoll::Denied => bail!("Sign-in was declined in the browser."),
            TokenPoll::Expired => bail!("The code expired before it was approved. Start again."),
            TokenPoll::Token(token) => {
                keychain::save_token(&s.api_base_url, &token)?;
                remember_token(app, &s.api_base_url, Some(token));
                return status(app).await;
            }
        }
    }
}

pub fn cancel<R: Runtime>(app: &AppHandle<R>) {
    app.state::<AppState>().sign_in_cancel.store(true, Ordering::SeqCst);
}

pub fn sign_out<R: Runtime>(app: &AppHandle<R>) -> Result<()> {
    let s = settings::load(app)?;
    keychain::clear_token(&s.api_base_url)?;
    remember_token(app, &s.api_base_url, None);
    Ok(())
}
