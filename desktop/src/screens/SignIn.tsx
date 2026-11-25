import { useEffect, useRef, useState } from "react";
import { backend, errorMessage } from "../lib/backend";
import type { AuthStatus, DeviceStart, InstallRequest } from "../lib/types";

type Props = {
  apiBaseUrl: string;
  pendingInstall: InstallRequest | null;
  initialError: string | null;
  onSignedIn: (status: AuthStatus) => void;
  onOpenSettings: () => void;
};

type Phase = { kind: "idle" } | { kind: "starting" } | { kind: "waiting"; code: DeviceStart } | { kind: "error"; message: string };

export function SignIn({ apiBaseUrl, pendingInstall, initialError, onSignedIn, onOpenSettings }: Props) {
  const [phase, setPhase] = useState<Phase>(initialError ? { kind: "error", message: initialError } : { kind: "idle" });
  const alive = useRef(true);

  useEffect(() => {
    alive.current = true;
    return () => {
      alive.current = false;
    };
  }, []);

  async function start() {
    setPhase({ kind: "starting" });
    let code: DeviceStart;
    try {
      code = await backend.signInStart();
    } catch (e) {
      setPhase({ kind: "error", message: errorMessage(e) });
      return;
    }
    if (!alive.current) return;
    setPhase({ kind: "waiting", code });
    try {
      const status = await backend.signInWait(code);
      if (!alive.current) return;
      if (status.signedIn) {
        onSignedIn(status);
      } else {
        setPhase({ kind: "error", message: status.error ?? "Sign-in did not complete." });
      }
    } catch (e) {
      if (!alive.current) return;
      setPhase({ kind: "error", message: errorMessage(e) });
    }
  }

  async function cancel() {
    await backend.signInCancel();
    setPhase({ kind: "idle" });
  }

  return (
    <div className="signin">
      <h1>Sign in to Pangea</h1>
      <p className="muted">
        Pangea builds the plugins you made on the web into Logic on this Mac. Sign in with the account you use at{" "}
        <span className="mono selectable">{apiBaseUrl}</span>.
      </p>

      {pendingInstall && (
        <div className="notice warn">
          A link asked to install project <b className="mono">{pendingInstall.project}</b>
          {pendingInstall.version != null ? ` v${pendingInstall.version}` : ""}. It will start after you sign in.
        </div>
      )}

      {phase.kind === "idle" && (
        <div className="actions">
          <button className="primary" onClick={start}>
            Sign in
          </button>
          <button className="quiet" onClick={onOpenSettings}>
            Settings
          </button>
        </div>
      )}

      {phase.kind === "starting" && <p className="muted">Asking Pangea for a code…</p>}

      {phase.kind === "waiting" && (
        <div>
          <p>Your browser should have opened. If it asks for a code, enter this one:</p>
          <div className="usercode">{phase.code.userCode}</div>
          <p className="muted">
            Or open{" "}
            <a
              href="#open"
              onClick={(e) => {
                e.preventDefault();
                void backend.openUrl(phase.code.verificationUriComplete);
              }}
            >
              {phase.code.verificationUri}
            </a>{" "}
            yourself. Waiting for approval…
          </p>
          <div className="actions">
            <button onClick={() => void backend.openUrl(phase.code.verificationUriComplete)}>Open the browser again</button>
            <button className="quiet" onClick={cancel}>
              Cancel
            </button>
          </div>
        </div>
      )}

      {phase.kind === "error" && (
        <div>
          <div className="notice bad selectable">{phase.message}</div>
          <div className="actions">
            <button className="primary" onClick={start}>
              Try again
            </button>
            <button className="quiet" onClick={onOpenSettings}>
              Settings
            </button>
          </div>
        </div>
      )}
    </div>
  );
}
