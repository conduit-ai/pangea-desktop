//! The bearer token lives in the macOS keychain: service "Pangea", account = API base URL.
//! Nothing here ever writes the token to a file.

use anyhow::{Context, Result};
use keyring::{Entry, Error as KeyringError};

const SERVICE: &str = "Pangea";

fn entry(api_base_url: &str) -> Result<Entry> {
    Entry::new(SERVICE, api_base_url).context("could not open the keychain entry")
}

pub fn save_token(api_base_url: &str, token: &str) -> Result<()> {
    entry(api_base_url)?
        .set_password(token)
        .context("could not store the token in the keychain")
}

/// Reads the token. `Err(AccessRefused)` means an item exists but macOS did not let this
/// binary read it (the user pressed Deny, or the prompt could not be shown): that happens
/// after the app is rebuilt in development, because the keychain ties access to the
/// binary's signature. The caller removes the stale item and asks for a fresh sign-in.
pub fn load_token(api_base_url: &str) -> Result<Option<String>> {
    match entry(api_base_url)?.get_password() {
        Ok(t) if !t.is_empty() => Ok(Some(t)),
        Ok(_) => Ok(None),
        Err(KeyringError::NoEntry) => Ok(None),
        Err(e) => Err(anyhow::anyhow!(AccessRefused(e.to_string()))),
    }
}

#[derive(Debug)]
pub struct AccessRefused(pub String);

impl std::fmt::Display for AccessRefused {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "the keychain did not let this build of Pangea read the saved token ({})", self.0)
    }
}

impl std::error::Error for AccessRefused {}

pub fn is_access_refused(e: &anyhow::Error) -> bool {
    e.downcast_ref::<AccessRefused>().is_some()
}

pub fn clear_token(api_base_url: &str) -> Result<()> {
    match entry(api_base_url)?.delete_credential() {
        Ok(()) | Err(KeyringError::NoEntry) => Ok(()),
        Err(e) => Err(anyhow::anyhow!("could not remove the token from the keychain: {e}")),
    }
}
