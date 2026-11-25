import { useCallback, useEffect, useRef, useState } from "react";
import { STAGE_WORDS } from "../components/HostPlugin";
import { fetchLine } from "../components/ToolsFetch";
import { backend, errorMessage, events } from "../lib/backend";
import type { SetupAction, SetupStatus, SetupStep } from "../lib/types";

/** What each stage of the self-check is called while it runs. */
const CHECK_WORDS: Record<string, string> = {
  module: "Building a small test plugin",
  host: "Opening test plugins in the Pangea plugin (a Pangea window shows for a few seconds)",
  installed: "Comparing the installed Pangea plugin with the one just checked",
  done: "Done",
};

const MARKS: Record<SetupStep["state"], string> = { done: "✓", doing: "…", "needs-you": "●", failed: "✕", waiting: "·" };
const STATE_WORDS: Record<SetupStep["state"], string> = {
  done: "Done",
  doing: "Under way",
  "needs-you": "Needs you",
  failed: "Stopped",
  waiting: "Waiting",
};

type Props = {
  /** The sign-in step's button: the app's own sign-in screen, then back here. */
  onSignIn: () => void;
  onOpenSettings: () => void;
  onStatus?: (s: SetupStatus) => void;
};

/**
 * Setup (the plan's section 6, src-tauri/src/setup.rs): the five steps from a fresh Mac to a
 * working Pangea plugin, each with its state and how long it usually takes, and one main button
 * that does the next thing. Nothing here asks the member to type a command.
 */
export function Setup({ onSignIn, onOpenSettings, onStatus }: Props) {
  const [status, setStatus] = useState<SetupStatus | null>(null);
  const [running, setRunning] = useState<string | null>(null);
  const [progress, setProgress] = useState<Record<string, string>>({});
  const [errors, setErrors] = useState<Record<string, string>>({});
  const [copied, setCopied] = useState<string | null>(null);
  const lines = useRef<string[]>([]);
  const onStatusRef = useRef(onStatus);
  onStatusRef.current = onStatus;

  const refresh = useCallback(async () => {
    try {
      const s = await backend.setupStatus();
      setStatus(s);
      onStatusRef.current?.(s);
    } catch (e) {
      setErrors((prev) => ({ ...prev, setup: errorMessage(e) }));
    }
  }, []);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  // Look again often while something is under way (Apple's installer, a download, a build).
  const busy = Boolean(status?.busy || running);
  useEffect(() => {
    const t = setInterval(() => void refresh(), busy ? 2000 : 10000);
    return () => clearInterval(t);
  }, [busy, refresh]);

  // Progress from the steps that run in the app.
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
    const line = (l: [string, string]) => {
      lines.current = [...lines.current.slice(-300), l[1]];
    };
    keep(events.onToolFetch((f) => setProgress((p) => ({ ...p, tools: fetchLine(f) }))));
    keep(events.onHostInstallStage((s) => setProgress((p) => ({ ...p, plugin: STAGE_WORDS[s] ?? s }))));
    keep(events.onSelfCheckStage((s) => setProgress((p) => ({ ...p, check: CHECK_WORDS[s] ?? s }))));
    keep(events.onHostInstallLog(line));
    keep(events.onSelfCheckLog(line));
    return () => {
      active = false;
      for (const u of unlisten) u();
    };
  }, []);

  async function act(a: SetupAction, step: string) {
    if (a.id === "sign-in") return onSignIn();
    if (a.id === "open-settings") return onOpenSettings();
    setRunning(a.id);
    setErrors((prev) => ({ ...prev, [step]: "" }));
    setProgress((prev) => ({ ...prev, [step]: "" }));
    lines.current = [];
    try {
      switch (a.id) {
        case "install-apple":
          await backend.appleToolsInstall();
          break;
        case "check-apple":
          await backend.appleTools();
          break;
        case "open-xcode":
          await backend.openXcode();
          break;
        case "get-tools":
          await backend.getTools();
          break;
        case "install-plugin":
          await backend.installHostPlugin(false);
          break;
        case "install-plugin-anyway":
          await backend.installHostPlugin(true);
          break;
        case "unpack-again":
          await backend.retryUnpack();
          break;
        case "run-check":
          await backend.runSelfCheck();
          break;
      }
    } catch (e) {
      setErrors((prev) => ({ ...prev, [step]: errorMessage(e) }));
    } finally {
      setRunning(null);
      await refresh();
    }
  }

  async function copyDetails(s: SetupStep) {
    try {
      await navigator.clipboard.writeText([`${s.title}: ${s.detail}`, errors[s.id] ?? "", s.details, "", ...lines.current].join("\n").trim());
      setCopied(s.id);
      setTimeout(() => setCopied(null), 2000);
    } catch (e) {
      setErrors((prev) => ({ ...prev, [s.id]: errorMessage(e) }));
    }
  }

  if (!status) return <p className="muted">Looking at this Mac…</p>;
  const nextStep = status.next ? status.steps.find((s) => s.action?.id === status.next?.id) : undefined;

  return (
    <div className="setup">
      <div className="panel">
        <h1>Set up Pangea on this Mac</h1>
        <p className="muted">
          Pangea makes your plugins on your Mac. These steps get it ready, once. None of them needs Terminal, and Pangea does the waiting.
        </p>
        <div className="row">
          {status.complete ? (
            <span className="good">Pangea is ready. Your plugins open in the Pangea plugin in your DAW.</span>
          ) : status.next && nextStep ? (
            <button className="primary big" onClick={() => act(status.next as SetupAction, nextStep.id)} disabled={running !== null}>
              {status.next.label}
            </button>
          ) : (
            <span className="muted">Waiting for the step under way.</span>
          )}
        </div>
        {errors.setup && <p className="error selectable">{errors.setup}</p>}
      </div>

      <ol className="steps">
        {status.steps.map((s, n) => (
          <li key={s.id} className={`step state-${s.state}`}>
            <div className="mark" aria-label={STATE_WORDS[s.state]}>
              {MARKS[s.state]}
            </div>
            <div className="body">
              <div className="row between">
                <h2>
                  {n + 1}. {s.title}
                </h2>
                <span className="muted">
                  {STATE_WORDS[s.state]}
                  {s.estimate && s.state !== "done" && <> · {s.estimate}</>}
                </span>
              </div>
              <p className="selectable">{s.detail}</p>
              {s.daw && (
                <p className={`daw daw-${s.daw.state} selectable`}>
                  <span className="daw-mark" aria-label={s.daw.state === "done" ? "Done" : "Waiting"}>
                    {s.daw.state === "done" ? "✓" : "·"}
                  </span>{" "}
                  {s.daw.text}
                </p>
              )}
              {s.state === "doing" && progress[s.id] && <p className="muted">{progress[s.id]}…</p>}
              {errors[s.id] && <p className="error selectable">{errors[s.id]}</p>}
              {(s.action || s.extra || s.link || s.state === "failed") && (
                <div className="row">
                  {s.action && s.action.id !== status.next?.id && (
                    <button onClick={() => act(s.action as SetupAction, s.id)} disabled={running !== null || s.state === "waiting"}>
                      {s.action.label}
                    </button>
                  )}
                  {s.extra && (
                    <button className="quiet" onClick={() => act(s.extra as SetupAction, s.id)} disabled={running !== null}>
                      {s.extra.label}
                    </button>
                  )}
                  {s.link && (
                    <button
                      className="quiet"
                      onClick={() => backend.openUrl(s.link?.url ?? "").catch((e) => setErrors((prev) => ({ ...prev, [s.id]: errorMessage(e) })))}
                    >
                      {s.link.label}
                    </button>
                  )}
                  {(s.state === "failed" || errors[s.id]) && (
                    <button className="quiet" onClick={() => copyDetails(s)}>
                      {copied === s.id ? "Copied" : "Copy details"}
                    </button>
                  )}
                </div>
              )}
            </div>
          </li>
        ))}
      </ol>
    </div>
  );
}
