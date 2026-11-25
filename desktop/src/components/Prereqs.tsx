import type { Check } from "../lib/types";

const marks: Record<Check["status"], string> = { ok: "✓", warn: "!", missing: "✕" };

export function PrereqList({ checks }: { checks: Check[] }) {
  if (checks.length === 0) return <p className="muted">Checking…</p>;
  return (
    <div className="checks">
      {checks.map((c) => (
        <div className="check" key={c.id}>
          <div className={`mark status-${c.status}`}>{marks[c.status]}</div>
          <div className="name">{c.name}</div>
          <div>
            <div className="detail selectable">{c.detail}</div>
            {c.fix && (
              <div className="fix">
                <code>{c.fix}</code>
              </div>
            )}
          </div>
        </div>
      ))}
    </div>
  );
}

/** A short banner for the Projects screen when something is missing. */
export function PrereqBanner({ checks, onOpenSettings }: { checks: Check[]; onOpenSettings: () => void }) {
  const missing = checks.filter((c) => c.status === "missing");
  const warn = checks.filter((c) => c.status === "warn");
  if (missing.length === 0 && warn.length === 0) return null;
  const kind = missing.length > 0 ? "bad" : "warn";
  return (
    <div className={`notice ${kind}`}>
      <div className="row between">
        <div>
          {missing.length > 0 ? (
            <span>
              Missing before a build can run: <b>{missing.map((c) => c.name).join(", ")}</b>.
            </span>
          ) : (
            <span>
              Worth a look: <b>{warn.map((c) => c.name).join(", ")}</b>.
            </span>
          )}{" "}
          <span className="muted">Setup shows what to do next.</span>
        </div>
        <button className="quiet" onClick={onOpenSettings}>
          Open Settings
        </button>
      </div>
    </div>
  );
}
