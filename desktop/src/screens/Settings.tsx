import { useEffect, useState } from "react";
import { AboutPanel } from "../components/About";
import { AppleToolsPanel } from "../components/AppleTools";
import { HostPluginPanel } from "../components/HostPlugin";
import { PrereqList } from "../components/Prereqs";
import { ToolsFetchPanel } from "../components/ToolsFetch";
import { backend, errorMessage } from "../lib/backend";
import type { AuthStatus, Check, Settings as SettingsType, ShellInfo } from "../lib/types";

type Props = {
  settings: SettingsType;
  checks: Check[];
  auth: AuthStatus;
  onSaved: (s: SettingsType) => void;
  onRecheck: () => Promise<void>;
  onSignOut: () => void;
};

export function Settings({ settings, checks, auth, onSaved, onRecheck, onSignOut }: Props) {
  const [apiBaseUrl, setApiBaseUrl] = useState(settings.apiBaseUrl);
  const [shellDir, setShellDir] = useState(settings.shellDir);
  const [projectsDir, setProjectsDir] = useState("");
  const [shell, setShell] = useState<ShellInfo | null>(null);
  const [message, setMessage] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [checking, setChecking] = useState(false);

  useEffect(() => {
    setApiBaseUrl(settings.apiBaseUrl);
    setShellDir(settings.shellDir);
  }, [settings]);

  useEffect(() => {
    backend.projectsDir().then(setProjectsDir).catch(() => setProjectsDir(""));
  }, []);

  const dirty = apiBaseUrl !== settings.apiBaseUrl || shellDir !== settings.shellDir;

  async function save() {
    setError(null);
    setMessage(null);
    try {
      const saved = await backend.setSettings({ apiBaseUrl, shellDir });
      onSaved(saved);
      setMessage("Saved.");
      await recheck();
    } catch (e) {
      setError(errorMessage(e));
    }
  }

  async function recheck() {
    setChecking(true);
    try {
      await onRecheck();
    } finally {
      setChecking(false);
    }
  }

  useEffect(() => {
    backend
      .shellInfo()
      .then(setShell)
      .catch(() => setShell(null));
  }, [settings]);

  async function useDefaults() {
    const d = await backend.defaultSettings();
    setApiBaseUrl(d.apiBaseUrl);
    setShellDir(d.shellDir);
  }

  return (
    <div className="settings">
      <div className="panel">
        <h2>Connection</h2>
        <div className="field">
          <label>Pangea web app</label>
          <input type="url" value={apiBaseUrl} onChange={(e) => setApiBaseUrl(e.target.value)} spellCheck={false} />
          <div className="hint">
            The address you sign in to Pangea at: http://localhost:3000 while developing, the live site otherwise. Changing it signs you out of the old one; the
            token is kept per address in the keychain.
          </div>
        </div>
        <div className="field">
          <label>The shell</label>
          <div className="hint selectable">
            {shell?.source === "app" && <>Pangea's own{shell.version && <> ({shell.version})</>}, which comes with the app.</>}
            {shell?.source === "own" && <>Your own folder, {shell.dir}, in place of Pangea's own.</>}
            {shell?.source === "none" && <>None: this development build carries no shell. Name the repository's shell folder below.</>}
          </div>
          <details className="developers" open={settings.shellDir !== ""}>
            <summary>For developers: a shell folder of your own</summary>
            <div className="path">
              <input type="text" value={shellDir} onChange={(e) => setShellDir(e.target.value)} placeholder="Empty: Pangea's own" spellCheck={false} />
              {shellDir !== "" && (
                <button className="quiet" onClick={() => setShellDir("")}>
                  Use Pangea's own
                </button>
              )}
            </div>
            <div className="hint">
              The repository's shell folder, with build.sh and JUCE inside it. Builds then use it in place of the copy that comes with the
              app. Save to keep it.
            </div>
          </details>
        </div>
        <div className="row">
          <button className="primary" onClick={save} disabled={!dirty}>
            Save
          </button>
          <button className="quiet" onClick={useDefaults}>
            Defaults
          </button>
          {message && <span className="muted">{message}</span>}
          {error && <span className="error selectable">{error}</span>}
        </div>
      </div>

      <div className="panel">
        <div className="panel-title">
          <h2>Tools on this Mac</h2>
          <button onClick={recheck} disabled={checking}>
            {checking ? "Checking…" : "Check again"}
          </button>
        </div>
        <p className="muted">A native build needs these.</p>
        <AppleToolsPanel
          onChange={(t) => {
            // The list below says the same; check it again once the tools change state.
            if (t.state === "ready" && checks.some((c) => c.id === "xcode" && c.status !== "ok")) void recheck();
          }}
        />
        <ToolsFetchPanel onDone={() => void recheck()} />
        <PrereqList checks={checks} />
      </div>

      <div className="panel">
        <h2>The Pangea plugin</h2>
        <HostPluginPanel />
      </div>

      <div className="panel">
        <h2>Account</h2>
        {auth.signedIn ? (
          <div className="row between">
            <span>
              Signed in as <b className="selectable">{auth.email ?? "(email unknown)"}</b>
              {auth.error && <span className="error"> · {auth.error}</span>}
            </span>
            <button onClick={onSignOut}>Sign out</button>
          </div>
        ) : (
          <p className="muted">Not signed in.</p>
        )}
      </div>

      <div className="panel">
        <h2>Files</h2>
        <p className="muted">
          Projects are written to <span className="mono selectable">{projectsDir || "~/Library/Application Support/Pangea/projects"}</span>{" "}
          before they are built. Built plugins go to <span className="mono">~/Library/Audio/Plug-Ins</span>.
        </p>
      </div>

      <div className="panel">
        <h2>About Pangea</h2>
        <AboutPanel />
      </div>
    </div>
  );
}
