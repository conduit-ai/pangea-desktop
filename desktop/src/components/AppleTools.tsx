import { useCallback, useEffect, useRef, useState } from "react";
import { backend, errorMessage } from "../lib/backend";
import type { AppleTools as AppleToolsState } from "../lib/types";

/** While Apple's installer runs, how often the app looks again. */
const POLL_MS = 5000;

/**
 * Apple's build tools: the state, and the one thing to do next. The member never types a
 * command: Install starts Apple's own installer, and the app watches for it to finish.
 */
export function AppleToolsPanel({ onChange }: { onChange?: (t: AppleToolsState) => void }) {
  const [tools, setTools] = useState<AppleToolsState | null>(null);
  const [busy, setBusy] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [copied, setCopied] = useState(false);
  const [startedAt, setStartedAt] = useState<number | null>(null);
  const [now, setNow] = useState(Date.now());
  const onChangeRef = useRef(onChange);
  onChangeRef.current = onChange;

  const show = useCallback((t: AppleToolsState) => {
    setTools(t);
    onChangeRef.current?.(t);
  }, []);

  const check = useCallback(async () => {
    setError(null);
    try {
      show(await backend.appleTools());
    } catch (e) {
      setError(errorMessage(e));
    }
  }, [show]);

  useEffect(() => {
    void check();
  }, [check]);

  // While the installer runs, look again every few seconds; it turns to a tick by itself.
  const installing = tools?.state === "installing";
  useEffect(() => {
    if (!installing) return;
    const poll = setInterval(() => void check(), POLL_MS);
    const tick = setInterval(() => setNow(Date.now()), 1000);
    return () => {
      clearInterval(poll);
      clearInterval(tick);
    };
  }, [installing, check]);

  async function run(label: string, f: () => Promise<void>) {
    setBusy(label);
    setError(null);
    try {
      await f();
    } catch (e) {
      setError(errorMessage(e));
    } finally {
      setBusy(null);
    }
  }

  const install = () =>
    run("install", async () => {
      setStartedAt(Date.now());
      show(await backend.appleToolsInstall());
    });

  async function copyDetails() {
    if (!tools) return;
    try {
      await navigator.clipboard.writeText(`${tools.message}\n\n${tools.details}`.trim());
      setCopied(true);
      setTimeout(() => setCopied(false), 2000);
    } catch (e) {
      setError(errorMessage(e));
    }
  }

  if (!tools) return <p className="muted">Checking Apple's build tools…</p>;

  const minutes = startedAt ? Math.floor((now - startedAt) / 60000) : null;
  return (
    <div className="apple-tools">
      <p className={tools.state === "ready" ? "" : "selectable"}>{tools.message}</p>
      {tools.state === "ready" && (
        <p className="muted selectable">
          {tools.compiler ?? "Apple clang"}
          {tools.sdk && <>, with the SDK {tools.sdk}</>}
          {tools.developerDirOverride && <>, from {tools.developerDirOverride}</>}
        </p>
      )}
      {installing && (
        <p className="muted">
          Waiting for Apple's installer
          {minutes !== null && minutes > 0 && <> ({minutes === 1 ? "1 minute" : `${minutes} minutes`} so far)</>}. It usually takes{" "}
          {tools.estimate ?? "5 to 30 minutes"}.
        </p>
      )}
      <div className="row">
        {(tools.state === "missing" || tools.state === "stopped" || tools.state === "broken") && (
          <button className="primary" onClick={install} disabled={busy !== null}>
            {busy === "install" ? "Starting Apple's installer…" : "Install Apple's build tools"}
          </button>
        )}
        {tools.state === "licence" && (
          <button className="primary" onClick={() => run("xcode", () => backend.openXcode())} disabled={busy !== null}>
            Open Xcode
          </button>
        )}
        {tools.state !== "ready" && (
          <button onClick={() => run("check", check)} disabled={busy !== null}>
            {busy === "check" ? "Checking…" : "Check again"}
          </button>
        )}
        {tools.helpUrl && (
          <button className="quiet" onClick={() => run("help", () => backend.openUrl(tools.helpUrl ?? ""))}>
            Apple's download page
          </button>
        )}
        {tools.details && tools.state !== "ready" && (
          <button className="quiet" onClick={copyDetails}>
            {copied ? "Copied" : "Copy details"}
          </button>
        )}
      </div>
      {error && <p className="error selectable">{error}</p>}
    </div>
  );
}
