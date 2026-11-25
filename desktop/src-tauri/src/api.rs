//! The Pangea web API as the desktop app sees it (docs/architecture.md, "API for the desktop app").
//! Field names are read leniently where the architecture doc leaves room, and the
//! exact expectations are listed in desktop/README.md.

use std::time::Duration;

use anyhow::{anyhow, bail, Context, Result};
use reqwest::{Client, Method, RequestBuilder, StatusCode, Url};
use serde::{Deserialize, Serialize};
use serde_json::Value;

pub const CLIENT_ID: &str = "pangea-desktop";
const DEVICE_GRANT: &str = "urn:ietf:params:oauth:grant-type:device_code";

pub struct Api {
    base: String,
    token: Option<String>,
    client: Client,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceCodeResponse {
    pub device_code: String,
    pub user_code: String,
    pub verification_uri: String,
    pub verification_uri_complete: String,
    pub expires_in: u64,
    pub interval: u64,
}

#[derive(Debug, Deserialize)]
struct DeviceCodeWire {
    device_code: String,
    user_code: String,
    verification_uri: String,
    verification_uri_complete: Option<String>,
    expires_in: Option<u64>,
    interval: Option<u64>,
}

#[derive(Debug, Deserialize)]
struct TokenWire {
    access_token: String,
}

#[derive(Debug, Deserialize)]
struct OAuthError {
    error: String,
    error_description: Option<String>,
}

/// One turn of the device token poll.
#[derive(Debug)]
pub enum TokenPoll {
    Token(String),
    Pending,
    SlowDown,
    Denied,
    Expired,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Me {
    pub id: String,
    pub email: String,
    #[serde(default)]
    pub name: Option<String>,
}

/// Who made a project: from the projects list or from plugin.json's `owner`.
#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct Owner {
    pub id: Option<String>,
    pub name: Option<String>,
    pub email: Option<String>,
}

impl Owner {
    pub fn is_empty(&self) -> bool {
        self.id.is_none() && self.name.is_none() && self.email.is_none()
    }

    /// "Name (email)", or whichever of the two is known, or "an unknown member".
    pub fn display(&self) -> String {
        match (&self.name, &self.email) {
            (Some(n), Some(e)) if n != e => format!("{n} ({e})"),
            (Some(n), _) => n.clone(),
            (None, Some(e)) => e.clone(),
            (None, None) => self.id.clone().unwrap_or_else(|| "an unknown member".into()),
        }
    }

    /// True only when a field that identifies a person matches.
    pub fn is(&self, me: &Me) -> bool {
        (self.id.is_some() && self.id.as_deref() == Some(me.id.as_str()))
            || (self.email.is_some()
                && self.email.as_deref().map(|e| e.to_lowercase()) == Some(me.email.to_lowercase()))
    }

    fn from_value(v: Option<&Value>) -> Owner {
        match v {
            Some(Value::String(s)) => {
                if s.contains('@') {
                    Owner { email: Some(s.clone()), ..Default::default() }
                } else {
                    Owner { name: Some(s.clone()), ..Default::default() }
                }
            }
            Some(o @ Value::Object(_)) => Owner {
                id: str_of(o, &["id", "userId"]),
                name: str_of(o, &["name"]),
                email: str_of(o, &["email"]),
            },
            _ => Owner::default(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Project {
    pub id: String,
    pub name: String,
    pub slug: String,
    pub kind: String,
    pub visibility: String,
    pub head_version: Option<u32>,
    /// The owner's name or email, for the list.
    pub owner: String,
    pub owner_details: Owner,
    pub description: Option<String>,
    pub updated_at: Option<String>,
    /// Where the plugin is filed (decision 85): the category's id and the name people read,
    /// its tags, the other words a search for the category must find, and the head version's
    /// own summary. `None` and empty for a plugin that is not filed, and from a site that
    /// does not send them yet.
    pub category: Option<String>,
    pub category_name: Option<String>,
    #[serde(default)]
    pub tags: Vec<String>,
    #[serde(default)]
    pub keywords: Vec<String>,
    pub summary: Option<String>,
}

/// `GET /api/desktop/projects/:id/versions/:number`
#[derive(Debug, Clone)]
pub struct VersionBundle {
    pub version_id: String,
    pub number: u32,
    pub slug: String,
    pub plugin: Value,
    pub faust: String,
    pub logic: Option<String>,
    pub ui: Value,
    /// From plugin.json's `owner` (`{ "name", "email" }`), when the web side includes it.
    pub owner: Owner,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CreateBuild<'a> {
    pub project_id: &'a str,
    pub version_id: &'a str,
    pub target: &'a str,
    pub machine: &'a str,
}

/// A module the app has for the Pangea plugin (decision 87): a project's version, reported to
/// `POST /api/desktop/host-modules` so the site's build view knows the plugin has it.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ModuleReport {
    pub project_id: String,
    pub version: u32,
}

/// The most modules one report carries: the site takes no more in one call.
pub const MODULE_REPORT_MAX: usize = 500;

#[derive(Serialize)]
struct ModuleReports<'a> {
    modules: &'a [ModuleReport],
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PatchBuild<'a> {
    pub status: &'a str,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub log: Option<&'a str>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub faust_version: Option<&'a str>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub shell_version: Option<&'a str>,
}

impl Api {
    pub fn new(base: &str, token: Option<String>) -> Result<Self> {
        let client = Client::builder()
            .timeout(Duration::from_secs(30))
            .user_agent(format!("pangea-desktop/{}", env!("CARGO_PKG_VERSION")))
            .build()
            .context("could not create the HTTP client")?;
        Ok(Self { base: base.trim_end_matches('/').to_string(), token, client })
    }

    pub fn base(&self) -> &str {
        &self.base
    }

    /// The base URL with `segments` appended as path segments, each percent-encoded
    /// (a `/`, `?` or `#` inside a segment cannot change the route).
    fn url(&self, segments: &[&str]) -> Result<Url> {
        url_for(&self.base, segments)
    }

    fn request(&self, method: Method, segments: &[&str]) -> Result<RequestBuilder> {
        let mut rb = self.client.request(method, self.url(segments)?);
        if let Some(t) = &self.token {
            rb = rb.bearer_auth(t);
        }
        Ok(rb.header("Accept", "application/json"))
    }

    /// Sends and turns any non-2xx into an error that names the route and the
    /// server's message. 401 is surfaced as `Unauthorised` so callers can clear the token.
    async fn send_json<T: for<'de> Deserialize<'de>>(&self, rb: RequestBuilder, what: &str) -> Result<T> {
        let resp = rb.send().await.with_context(|| format!("{what}: could not reach {}", self.base))?;
        let status = resp.status();
        let body = resp.text().await.unwrap_or_default();
        if status == StatusCode::UNAUTHORIZED {
            return Err(anyhow!(Unauthorised));
        }
        if !status.is_success() {
            bail!("{what} failed: {} {}", status.as_u16(), server_message(&body));
        }
        serde_json::from_str::<T>(&body).with_context(|| format!("{what}: unexpected response: {}", clip(&body)))
    }

    // ---- device flow -----------------------------------------------------

    pub async fn device_code(&self) -> Result<DeviceCodeResponse> {
        let body = serde_json::json!({ "client_id": CLIENT_ID, "scope": "" });
        let wire: DeviceCodeWire = self
            .send_json(self.request(Method::POST, &["api", "auth", "device", "code"])?.json(&body), "POST /api/auth/device/code")
            .await?;
        let complete = wire.verification_uri_complete.clone().unwrap_or_else(|| {
            let sep = if wire.verification_uri.contains('?') { '&' } else { '?' };
            format!("{}{sep}user_code={}", wire.verification_uri, wire.user_code)
        });
        Ok(DeviceCodeResponse {
            device_code: wire.device_code,
            user_code: wire.user_code,
            verification_uri: wire.verification_uri,
            verification_uri_complete: complete,
            expires_in: wire.expires_in.unwrap_or(1800),
            interval: wire.interval.unwrap_or(5).max(1),
        })
    }

    pub async fn device_token(&self, device_code: &str) -> Result<TokenPoll> {
        let body = serde_json::json!({
            "grant_type": DEVICE_GRANT,
            "device_code": device_code,
            "client_id": CLIENT_ID,
        });
        let resp = self
            .request(Method::POST, &["api", "auth", "device", "token"])?
            .json(&body)
            .send()
            .await
            .with_context(|| format!("POST /api/auth/device/token: could not reach {}", self.base))?;
        let status = resp.status();
        let text = resp.text().await.unwrap_or_default();
        if status.is_success() {
            let t: TokenWire = serde_json::from_str(&text).context("device token: unexpected response")?;
            return Ok(TokenPoll::Token(t.access_token));
        }
        if let Ok(e) = serde_json::from_str::<OAuthError>(&text) {
            return Ok(match e.error.as_str() {
                "authorization_pending" => TokenPoll::Pending,
                "slow_down" => TokenPoll::SlowDown,
                "access_denied" => TokenPoll::Denied,
                "expired_token" => TokenPoll::Expired,
                other => bail!(
                    "sign-in failed: {other}{}",
                    e.error_description.map(|d| format!(": {d}")).unwrap_or_default()
                ),
            });
        }
        bail!("POST /api/auth/device/token failed: {} {}", status.as_u16(), server_message(&text));
    }

    // ---- desktop API -----------------------------------------------------

    pub async fn me(&self) -> Result<Me> {
        self.send_json(self.request(Method::GET, &["api", "desktop", "me"])?, "GET /api/desktop/me").await
    }

    pub async fn projects(&self) -> Result<Vec<Project>> {
        let v: Value = self
            .send_json(self.request(Method::GET, &["api", "desktop", "projects"])?, "GET /api/desktop/projects")
            .await?;
        let items = match &v {
            Value::Array(a) => a.clone(),
            Value::Object(o) => o
                .get("projects")
                .or_else(|| o.get("items"))
                .and_then(|p| p.as_array())
                .cloned()
                .unwrap_or_default(),
            _ => vec![],
        };
        Ok(items.iter().filter_map(project_from_value).collect())
    }

    pub async fn version(&self, project_id: &str, number: u32) -> Result<VersionBundle> {
        validate_project_id(project_id)?;
        validate_version(number)?;
        let number_s = number.to_string();
        let what = format!("GET /api/desktop/projects/{project_id}/versions/{number}");
        let v: Value = self
            .send_json(
                self.request(Method::GET, &["api", "desktop", "projects", project_id, "versions", &number_s])?,
                &what,
            )
            .await?;
        version_from_value(&v, project_id, number)
    }

    pub async fn create_build(&self, req: &CreateBuild<'_>) -> Result<String> {
        let v: Value = self
            .send_json(self.request(Method::POST, &["api", "desktop", "builds"])?.json(req), "POST /api/desktop/builds")
            .await?;
        id_of(&v).ok_or_else(|| anyhow!("POST /api/desktop/builds: no id in the response: {}", clip(&v.to_string())))
    }

    /// Tells the site which modules the Pangea plugin has, in calls of at most
    /// `MODULE_REPORT_MAX`. Projects that are not the member's own, and versions that do not
    /// exist, are left out by the site.
    pub async fn report_modules(&self, modules: &[ModuleReport]) -> Result<()> {
        for chunk in modules.chunks(MODULE_REPORT_MAX) {
            let _: Value = self
                .send_json(
                    self.request(Method::POST, &["api", "desktop", "host-modules"])?.json(&ModuleReports { modules: chunk }),
                    "POST /api/desktop/host-modules",
                )
                .await?;
        }
        Ok(())
    }

    pub async fn patch_build(&self, id: &str, req: &PatchBuild<'_>) -> Result<()> {
        let path = format!("/api/desktop/builds/{id}");
        let resp = self
            .request(Method::PATCH, &["api", "desktop", "builds", id])?
            .json(req)
            .send()
            .await
            .with_context(|| format!("PATCH {path}: could not reach {}", self.base))?;
        let status = resp.status();
        if status == StatusCode::UNAUTHORIZED {
            return Err(anyhow!(Unauthorised));
        }
        if !status.is_success() {
            let body = resp.text().await.unwrap_or_default();
            bail!("PATCH {path} failed: {} {}", status.as_u16(), server_message(&body));
        }
        Ok(())
    }
}

/// Appends `segments` to `base` as percent-encoded path segments.
pub fn url_for(base: &str, segments: &[&str]) -> Result<Url> {
    let mut url = Url::parse(base).with_context(|| format!("the API base URL is not a valid URL: {base}"))?;
    url.path_segments_mut()
        .map_err(|_| anyhow!("the API base URL cannot have paths: {base}"))?
        .pop_if_empty()
        .extend(segments);
    Ok(url)
}

/// The web app's project ids are UUIDs. Anything else is refused before any request
/// is built from it (a deep link is untrusted input).
pub fn validate_project_id(id: &str) -> Result<()> {
    let b = id.as_bytes();
    let ok = b.len() == 36
        && b.iter().enumerate().all(|(i, c)| match i {
            8 | 13 | 18 | 23 => *c == b'-',
            _ => c.is_ascii_hexdigit(),
        });
    if ok {
        Ok(())
    } else {
        bail!("not a valid project id: {}", clip(id))
    }
}

pub fn validate_version(number: u32) -> Result<()> {
    if number >= 1 {
        Ok(())
    } else {
        bail!("not a valid version number: {number} (versions start at 1)")
    }
}

/// Marker error for a 401: the token is gone or stale.
#[derive(Debug)]
pub struct Unauthorised;

impl std::fmt::Display for Unauthorised {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "not signed in, or the session has expired")
    }
}

impl std::error::Error for Unauthorised {}

pub fn is_unauthorised(e: &anyhow::Error) -> bool {
    e.downcast_ref::<Unauthorised>().is_some()
}

fn server_message(body: &str) -> String {
    if let Ok(v) = serde_json::from_str::<Value>(body) {
        for key in ["error_description", "message", "error"] {
            if let Some(s) = v.get(key).and_then(|x| x.as_str()) {
                return s.to_string();
            }
        }
    }
    clip(body)
}

fn clip(s: &str) -> String {
    let s = s.trim();
    if s.chars().count() > 300 {
        format!("{}...", s.chars().take(300).collect::<String>())
    } else {
        s.to_string()
    }
}

fn str_of(v: &Value, keys: &[&str]) -> Option<String> {
    keys.iter().find_map(|k| v.get(*k).and_then(|x| x.as_str()).map(str::to_string))
}

/// The strings of an array field; anything else in it, and a field that is no array, is left out.
fn strs_of(v: &Value, key: &str) -> Vec<String> {
    v.get(key)
        .and_then(Value::as_array)
        .map(|items| items.iter().filter_map(|x| x.as_str()).map(str::to_string).collect())
        .unwrap_or_default()
}

fn id_of(v: &Value) -> Option<String> {
    match v.get("id") {
        Some(Value::String(s)) => Some(s.clone()),
        Some(Value::Number(n)) => Some(n.to_string()),
        _ => v.get("build").and_then(id_of),
    }
}

fn number_of(v: &Value) -> Option<u32> {
    match v {
        Value::Number(n) => n.as_u64().map(|n| n as u32),
        Value::String(s) => s.parse().ok(),
        Value::Object(o) => o.get("number").and_then(number_of),
        _ => None,
    }
}

fn project_from_value(v: &Value) -> Option<Project> {
    let id = str_of(v, &["id"])?;
    let name = str_of(v, &["name"]).unwrap_or_else(|| id.clone());
    let slug = str_of(v, &["slug"]).unwrap_or_else(|| id.clone());
    let head_version = v
        .get("headVersion")
        .or_else(|| v.get("headVersionNumber"))
        .or_else(|| v.get("head"))
        .and_then(number_of);
    let mut owner_details = Owner::from_value(v.get("owner"));
    if owner_details.is_empty() {
        owner_details = Owner {
            id: str_of(v, &["ownerId", "owner_id"]),
            name: str_of(v, &["ownerName"]),
            email: str_of(v, &["ownerEmail"]),
        };
    }
    let owner = if owner_details.is_empty() { String::new() } else { owner_details.display() };
    Some(Project {
        id,
        name,
        slug,
        kind: str_of(v, &["kind"]).unwrap_or_else(|| "effect".into()),
        visibility: str_of(v, &["visibility"]).unwrap_or_else(|| "private".into()),
        head_version,
        owner,
        owner_details,
        description: str_of(v, &["description"]),
        updated_at: str_of(v, &["updatedAt", "updated_at"]),
        category: str_of(v, &["category"]).filter(|s| !s.trim().is_empty()),
        category_name: str_of(v, &["categoryName"]).filter(|s| !s.trim().is_empty()),
        tags: strs_of(v, "tags"),
        keywords: strs_of(v, "keywords"),
        summary: str_of(v, &["summary"]).filter(|s| !s.trim().is_empty()),
    })
}

fn version_from_value(v: &Value, project_id: &str, requested: u32) -> Result<VersionBundle> {
    let plugin = v.get("plugin").cloned().unwrap_or(Value::Object(Default::default()));
    let faust = str_of(v, &["faust", "dsp"]).ok_or_else(|| anyhow!("the version has no Faust source"))?;
    let logic = str_of(v, &["logic", "logicCpp"]).filter(|s| !s.trim().is_empty());
    let ui = v.get("ui").cloned().unwrap_or(Value::Object(Default::default()));
    let version_id = str_of(v, &["versionId"])
        .or_else(|| v.get("version").and_then(|x| str_of(x, &["id"])))
        .or_else(|| str_of(&plugin, &["versionId"]))
        .or_else(|| str_of(v, &["id"]))
        .ok_or_else(|| {
            anyhow!("the version response has no versionId (expected `versionId` at the top level)")
        })?;
    let number = plugin
        .get("version")
        .or_else(|| plugin.get("versionNumber"))
        .or_else(|| v.get("number"))
        .and_then(number_of)
        .unwrap_or(requested);
    let slug = str_of(&plugin, &["slug"]).unwrap_or_else(|| project_id.to_string());
    let mut owner = Owner::from_value(plugin.get("owner"));
    if owner.is_empty() {
        owner = Owner::from_value(v.get("owner"));
    }
    Ok(VersionBundle { version_id, number, slug: safe_slug(&slug), plugin, faust, logic, ui, owner })
}

/// Keeps a slug safe to use as a directory name.
pub fn safe_slug(s: &str) -> String {
    let out: String = s
        .chars()
        .map(|c| if c.is_ascii_alphanumeric() || c == '-' || c == '_' { c } else { '-' })
        .collect();
    let out = out.trim_matches('-').to_string();
    if out.is_empty() {
        "project".to_string()
    } else {
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn project_ids_must_be_uuids() {
        assert!(validate_project_id("3f2a1c4e-9b7d-4e6a-8c1f-0a2b3c4d5e6f").is_ok());
        assert!(validate_project_id("3F2A1C4E-9B7D-4E6A-8C1F-0A2B3C4D5E6F").is_ok());
        for bad in ["../me", "p1", "", "3f2a1c4e-9b7d-4e6a-8c1f-0a2b3c4d5e6", "3f2a1c4e-9b7d-4e6a-8c1f-0a2b3c4d5e6f/x", "3f2a1c4e_9b7d_4e6a_8c1f_0a2b3c4d5e6f"] {
            assert!(validate_project_id(bad).is_err(), "should refuse {bad:?}");
        }
        assert!(validate_version(1).is_ok());
        assert!(validate_version(0).is_err());
    }

    #[test]
    fn urls_are_built_from_encoded_segments() {
        let u = url_for("http://localhost:3000", &["api", "desktop", "projects", "3f2a1c4e-9b7d-4e6a-8c1f-0a2b3c4d5e6f", "versions", "3"]).unwrap();
        assert_eq!(u.as_str(), "http://localhost:3000/api/desktop/projects/3f2a1c4e-9b7d-4e6a-8c1f-0a2b3c4d5e6f/versions/3");
        // a hostile segment cannot change the route: separators are encoded
        let u = url_for("https://pangea.audio/", &["api", "desktop", "builds", "a/b?c#d"]).unwrap();
        assert_eq!(u.as_str(), "https://pangea.audio/api/desktop/builds/a%2Fb%3Fc%23d");
        // a base with a path keeps it
        let u = url_for("https://example.com/pangea", &["api", "desktop", "me"]).unwrap();
        assert_eq!(u.as_str(), "https://example.com/pangea/api/desktop/me");
    }

    #[test]
    fn a_project_reads_where_it_is_filed() {
        let v = serde_json::json!({
            "id": "3f2a1c4e-9b7d-4e6a-8c1f-0a2b3c4d5e6f",
            "name": "Prime Delay",
            "slug": "prime-delay",
            "kind": "effect",
            "headVersionNumber": 12,
            "owner": { "id": "u1", "name": "Sam", "email": "sam@example.com" },
            "description": "Yes, it is possible.",
            "category": "delay",
            "categoryName": "Delay",
            "tags": ["tempo-synced", "saturation", 7],
            "keywords": ["echo"],
            "summary": "A stereo delay where each side repeats at a multiple of one unit time."
        });
        let p = project_from_value(&v).unwrap();
        assert_eq!(p.category.as_deref(), Some("delay"));
        assert_eq!(p.category_name.as_deref(), Some("Delay"));
        assert_eq!(p.tags, ["tempo-synced", "saturation"]);
        assert_eq!(p.keywords, ["echo"]);
        assert_eq!(p.summary.as_deref(), Some("A stereo delay where each side repeats at a multiple of one unit time."));
        assert_eq!(p.head_version, Some(12));
    }

    #[test]
    fn a_project_from_a_site_that_files_nothing_is_not_filed() {
        // The list as the site sent it before decision 85, and as it sends a plugin that is not filed.
        let before = serde_json::json!({ "id": "3f2a1c4e-9b7d-4e6a-8c1f-0a2b3c4d5e6f", "name": "Tape Drift", "description": "Warm." });
        let unfiled = serde_json::json!({
            "id": "3f2a1c4e-9b7d-4e6a-8c1f-0a2b3c4d5e6f", "name": "Tape Drift",
            "category": null, "categoryName": null, "tags": [], "keywords": [], "summary": ""
        });
        for v in [before, unfiled] {
            let p = project_from_value(&v).unwrap();
            assert_eq!(p.category, None);
            assert_eq!(p.category_name, None);
            assert!(p.tags.is_empty() && p.keywords.is_empty());
            assert_eq!(p.summary, None);
        }
    }

    #[test]
    fn an_owner_sent_without_an_email_address_is_still_known_by_id() {
        // Since profiles (docs/profiles.md) the site sends an owner's id, artist name and
        // handle, never their email address, in the list and in plugin.json.
        let me = Me { id: "u1".into(), email: "ada@example.com".into(), name: Some("Ada".into()) };
        let listed = serde_json::json!({
            "id": "3f2a1c4e-9b7d-4e6a-8c1f-0a2b3c4d5e6f", "name": "Prime Delay",
            "owner": { "id": "u1", "name": "Night Felt", "handle": "night-felt" }
        });
        let p = project_from_value(&listed).unwrap();
        assert!(p.owner_details.is(&me));
        assert_eq!(p.owner, "Night Felt");
        assert_eq!(p.owner_details.email, None);
        let theirs = serde_json::json!({
            "id": "3f2a1c4e-9b7d-4e6a-8c1f-0a2b3c4d5e6f", "name": "Prime Delay",
            "owner": { "id": "u2", "name": "", "handle": null }
        });
        let p = project_from_value(&theirs).unwrap();
        assert!(!p.owner_details.is(&me));

        let version = serde_json::json!({
            "versionId": "v1",
            "plugin": { "slug": "prime-delay", "version": 3, "owner": { "id": "u1", "name": "Night Felt" } },
            "faust": "process = _;"
        });
        let bundle = version_from_value(&version, "3f2a1c4e-9b7d-4e6a-8c1f-0a2b3c4d5e6f", 3).unwrap();
        assert!(!bundle.owner.is_empty());
        assert!(bundle.owner.is(&me), "the member's own version, known by the owner's id");
        assert_eq!(bundle.owner.display(), "Night Felt");
    }
}
