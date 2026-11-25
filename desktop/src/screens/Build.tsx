import { useEffect, useRef } from "react";
import { backend } from "../lib/backend";
import type { LogLine, QueueState } from "../lib/types";

type Props = {
  queue: QueueState;
  log: LogLine[];
  onClearHistory: () => void;
};

function jobLabel(name: string | null, projectId: string, version: number | null) {
  return `${name ?? projectId}${version != null ? ` v${version}` : " (head)"}`;
}

export function Build({ queue, log, onClearHistory }: Props) {
  const logRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const el = logRef.current;
    if (el) el.scrollTop = el.scrollHeight;
  }, [log.length]);

  const current = queue.current;
  const latest = queue.history[0] ?? null;
  const showLive = current !== null || (latest !== null && log.length > 0 && log[0]?.jobId === latest.id);

  return (
    <div className="build">
      <div className="panel">
        <div className="panel-title">
          <h2>Now</h2>
          {current && <span className="muted">{queue.queue.length > 0 ? `${queue.queue.length} waiting` : ""}</span>}
        </div>
        {current ? (
          <div className="job">
            <span className="spinner" />
            <span>
              Building <b>{jobLabel(current.projectName, current.projectId, current.version)}</b>
              {current.source === "link" ? <span className="muted"> (from a link)</span> : null}
            </span>
          </div>
        ) : latest ? (
          <div className={`notice ${latest.ok ? "good" : latest.outcome === "cancelled" ? "warn" : "bad"}`} style={{ marginBottom: 0 }}>
            <b>{jobLabel(latest.projectName, latest.projectId, latest.version)}</b>: {latest.message}
            {latest.ok &&
              [latest.component, latest.componentFx].filter(Boolean).map((path) => (
                <div key={path} className="muted mono selectable" style={{ marginTop: 4 }}>
                  {path}
                </div>
              ))}
          </div>
        ) : (
          <p className="muted">Nothing is building. Choose a project and press Install in Logic.</p>
        )}

        {queue.queue.length > 0 && (
          <div style={{ marginTop: 10 }}>
            <h3>Waiting</h3>
            {queue.queue.map((j) => (
              <div key={j.id} className="muted">
                {jobLabel(j.projectName, j.projectId, j.version)}
              </div>
            ))}
          </div>
        )}
      </div>

      {showLive && (
        <div className="panel">
          <div className="panel-title">
            <h2>Log</h2>
            {latest?.projectDir && !current && (
              <button className="quiet" onClick={() => void backend.openUrl(`file://${latest.projectDir}`)} title={latest.projectDir}>
                Project folder
              </button>
            )}
          </div>
          <div className="log" ref={logRef}>
            {log.length === 0 ? (
              <span className="muted">Waiting for output…</span>
            ) : (
              log.map((l, i) => (
                <div key={i} className={`l-${l.stream}`}>
                  {l.line}
                </div>
              ))
            )}
          </div>
        </div>
      )}

      {queue.history.length > 0 && (
        <div className="panel history">
          <div className="panel-title">
            <h2>Earlier</h2>
            <button className="quiet" onClick={onClearHistory}>
              Clear
            </button>
          </div>
          {queue.history.map((r) => (
            <div className="result" key={r.id}>
              <div className="row">
                <span className={r.ok ? "status-ok" : r.outcome === "cancelled" ? "status-warn" : "status-missing"}>
                  {r.ok ? "✓" : r.outcome === "cancelled" ? "·" : "✕"}
                </span>
                <span className="grow">
                  <b>{jobLabel(r.projectName, r.projectId, r.version)}</b> <span className="muted">· {r.message}</span>
                </span>
                <span className="muted">
                  {r.seconds != null ? `${Math.round(r.seconds)} s · ` : ""}
                  {new Date(r.finishedAt).toLocaleTimeString()}
                </span>
              </div>
              <details>
                <summary>Log</summary>
                <div className="log">{r.log}</div>
              </details>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
