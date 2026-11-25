import { useCallback, useEffect, useState } from "react";
import { backend, errorMessage, events } from "../lib/backend";
import type { HostInstallReport, HostPluginStatus } from "../lib/types";

/** What each stage of an install is called on the page. */
export const STAGE_WORDS: Record<string, string> = {
  build: "Building the Pangea plugin (a few minutes the first time, while JUCE compiles; seconds after)",
  install: "Putting it in your plug-in folders",
  registrar: "Telling macOS about the new Audio Units",
  auval: "Apple's check of the three Audio Units",
  done: "Done",
};

/** Subscribes to an install's stage and log lines while the component is shown. */
export function useInstallEvents(onStage: (s: string) => void, onLine: (l: string) => void) {
  useEffect(() => {
    let active = true;
    const unlisten: (() => void)[] = [];
    const keep = (p: Promise<() => void>) =>
      p
        .then((u) => {
          if (active) unlisten.push(u);
          else u();
        })
        .catch(() => undefined);
    keep(events.onHostInstallStage(onStage));
    keep(events.onHostInstallLog(([, line]) => onLine(line)));
    return () => {
      active = false;
      for (const u of unlisten) u();
    };
  }, [onStage, onLine]);
}

/**
 * The Pangea plugin: whether it is installed and right for this Pangea, and one button that
 * installs or updates it, with each stage as it happens and plain words at the end.
 */
export function HostPluginPanel({ onDone }: { onDone?: () => void }) {
  const [status, setStatus] = useState<HostPluginStatus | null>(null);
  const [stage, setStage] = useState<string | null>(null);
  const [lines, setLines] = useState<string[]>([]);
  const [report, setReport] = useState<HostInstallReport | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [copied, setCopied] = useState(false);

  const look = useCallback(async () => {
    try {
      setStatus(await backend.hostPlugin());
    } catch (e) {
      setError(errorMessage(e));
    }
  }, []);

  useEffect(() => {
    void look();
  }, [look]);

  const onLine = useCallback((l: string) => setLines((prev) => [...prev.slice(-400), l]), []);
  useInstallEvents(setStage, onLine);

  async function install(anyway: boolean) {
    setBusy(true);
    setError(null);
    setReport(null);
    setLines([]);
    setStage(null);
    try {
      const r = await backend.installHostPlugin(anyway);
      setReport(r);
      await look();
      if (r.ok) onDone?.();
    } catch (e) {
      setError(errorMessage(e));
    } finally {
      setBusy(false);
      setStage(null);
    }
  }

  async function copyDetails() {
    try {
      await navigator.clipboard.writeText([report?.message ?? error ?? "", "", ...lines].join("\n").trim());
      setCopied(true);
      setTimeout(() => setCopied(false), 2000);
    } catch (e) {
      setError(errorMessage(e));
    }
  }

  if (!status) return <p className="muted">Looking for the Pangea plugin…</p>;
  const p = status.plugin;
  const verb = p.state === "missing" ? "Install" : p.state === "installed" || p.state === "check-failed" ? "Install again" : "Update";
  const failed = report && !report.ok && report.stage !== "daw";

  return (
    <div className="host-plugin">
      <p className="selectable">{report?.ok ? report.message : p.message}</p>
      {busy && <p className="muted">{stage ? STAGE_WORDS[stage] ?? stage : "Starting"}…</p>}
      {report && !report.ok && <p className={report.stage === "daw" ? "warn selectable" : "error selectable"}>{report.message}</p>}
      {error && <p className="error selectable">{error}</p>}
      {!busy && (
        <div className="row">
          <button className={p.state === "installed" ? "" : "primary"} onClick={() => install(false)}>
            {verb} the Pangea plugin
          </button>
          {report?.stage === "daw" && (
            <button className="quiet" onClick={() => install(true)}>
              Install anyway
            </button>
          )}
          {(failed || error) && (
            <button className="quiet" onClick={copyDetails}>
              {copied ? "Copied" : "Copy details"}
            </button>
          )}
        </div>
      )}
      {busy && lines.length > 0 && <pre className="log tail selectable">{lines.slice(-6).join("\n")}</pre>}
    </div>
  );
}
