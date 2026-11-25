import { useCallback, useEffect, useState } from "react";
import { backend, errorMessage, events } from "../lib/backend";
import type { ToolFetch, ToolNeeds } from "../lib/types";

const MB = 1_000_000;

/** "Downloading CMake: 34 of 80 MB", or the step's own words. */
export function fetchLine(f: ToolFetch): string {
  if (f.stage === "download" && f.total > 0) {
    return `Downloading ${f.tool} ${f.version}: ${Math.floor(f.received / MB)} of ${Math.round(f.total / MB)} MB`;
  }
  return f.message;
}

/**
 * CMake and JUCE, fetched by the app the first time they are needed (src-tauri/src/tools.rs).
 * Shows what is still needed, one button that fetches it, the progress, and a plain sentence
 * when it stops. A download that stopped carries on from where it was.
 */
export function ToolsFetchPanel({ onDone }: { onDone?: () => void }) {
  const [needs, setNeeds] = useState<ToolNeeds | null>(null);
  const [progress, setProgress] = useState<ToolFetch | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const look = useCallback(async () => {
    try {
      const [n, last] = await backend.toolsNeeds();
      setNeeds(n);
      if (last && last.stage !== "ready" && last.stage !== "failed") setProgress(last);
    } catch (e) {
      setError(errorMessage(e));
    }
  }, []);

  useEffect(() => {
    void look();
    let active = true;
    let unlisten: (() => void) | null = null;
    events
      .onToolFetch(setProgress)
      .then((u) => {
        if (active) unlisten = u;
        else u();
      })
      .catch(() => undefined);
    return () => {
      active = false;
      unlisten?.();
    };
  }, [look]);

  async function getTools() {
    setBusy(true);
    setError(null);
    try {
      setNeeds(await backend.getTools());
      setProgress(null);
      onDone?.();
    } catch (e) {
      setError(errorMessage(e));
    } finally {
      setBusy(false);
    }
  }

  if (!needs) return null;
  const wanted = [needs.cmake && "CMake", needs.juce && "JUCE"].filter(Boolean).join(" and ");
  if (!wanted && !busy && !error) {
    return needs.juceInOwnShellMissing ? (
      <p className="muted">Your own shell folder has no JUCE. Pangea does not write into it; clone JUCE there, or use Pangea's own shell.</p>
    ) : null;
  }
  return (
    <div className="tools-fetch">
      {wanted && !busy && (
        <p>
          Pangea fetches {wanted} from {needs.cmake && needs.juce ? "their makers' releases" : "its maker's release"} the first time they
          are needed, about {needs.cmake && needs.juce ? "130" : needs.cmake ? "80" : "50"} MB. It takes a minute or two.
        </p>
      )}
      {busy && progress && <p className="muted">{fetchLine(progress)}…</p>}
      {busy && !progress && <p className="muted">Starting…</p>}
      {error && <p className="error selectable">{error}</p>}
      {!busy && wanted && (
        <button className="primary" onClick={getTools}>
          {error ? "Try again" : "Get Pangea's tools"}
        </button>
      )}
    </div>
  );
}
