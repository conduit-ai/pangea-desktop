import { useCallback, useEffect, useRef, useState } from "react";
import { ConfirmInstall } from "./components/ConfirmInstall";
import { backend, errorMessage, events } from "./lib/backend";
import type {
  AuthStatus,
  Check,
  ConfirmRequest,
  HostAdded,
  InstallRequest,
  LogLine,
  Project,
  QueueState,
  Settings as SettingsType,
} from "./lib/types";
import { Build } from "./screens/Build";
import { Projects } from "./screens/Projects";
import { Settings } from "./screens/Settings";
import { Setup } from "./screens/Setup";
import { SignIn } from "./screens/SignIn";

type Screen = "setup" | "signin" | "projects" | "build" | "settings";

const emptyQueue: QueueState = { current: null, queue: [], history: [] };

export function App() {
  const [settings, setSettings] = useState<SettingsType | null>(null);
  const [checks, setChecks] = useState<Check[]>([]);
  const [auth, setAuth] = useState<AuthStatus | null>(null);
  const [screen, setScreen] = useState<Screen>("projects");
  const [queue, setQueue] = useState<QueueState>(emptyQueue);
  const [log, setLog] = useState<LogLine[]>([]);
  const [pendingInstall, setPendingInstall] = useState<InstallRequest | null>(null);
  const [confirm, setConfirm] = useState<ConfirmRequest | null>(null);
  const [startupStep, setStartupStep] = useState("Loading settings");
  const [fatal, setFatal] = useState<string | null>(null);
  const [sessionNote, setSessionNote] = useState<string | null>(null);
  // After an add link from the site (decision 87): the version is ready in the Pangea plugin, or why not.
  const [added, setAdded] = useState<HostAdded | null>(null);
  // Setup (screens/Setup.tsx) is shown first until every step has a tick, then stays as a tab.
  const [setupLeft, setSetupLeft] = useState(0);
  const [afterSignIn, setAfterSignIn] = useState<Screen>("projects");
  const authRef = useRef<AuthStatus | null>(null);
  authRef.current = auth;

  const refreshChecks = useCallback(async () => {
    try {
      setChecks(await backend.checkPrerequisites());
    } catch (e) {
      setFatal(errorMessage(e));
    }
  }, []);

  const refreshAuth = useCallback(async () => {
    try {
      const a = await backend.authStatus();
      setAuth(a);
      return a;
    } catch (e) {
      setFatal(errorMessage(e));
      return null;
    }
  }, []);

  const startInstall = useCallback((projectId: string, version: number | null, projectName: string | null, source: "ui" | "link") => {
    setLog([]);
    setScreen("build");
    backend.buildAndInstall(projectId, version, projectName, source).catch((e) => setFatal(errorMessage(e)));
  }, []);

  // Deep links: install straight away when signed in, else remember and ask to sign in.
  const handleInstallRequest = useCallback(
    (r: InstallRequest) => {
      if (authRef.current?.signedIn) {
        startInstall(r.project, r.version, null, "link");
      } else {
        setPendingInstall(r);
      }
    },
    [startInstall],
  );

  useEffect(() => {
    // `listen` resolves after the cleanup may already have run (React StrictMode mounts
    // twice in development), so each subscription is dropped at once when that happens.
    let active = true;
    const unlisten: (() => void)[] = [];
    const subscribe = async (p: Promise<() => void>) => {
      const u = await p;
      if (active) unlisten.push(u);
      else u();
    };
    (async () => {
      try {
        setSettings(await backend.getSettings());
        setStartupStep("Checking sign-in");
        const a = await backend.authStatus();
        setAuth(a);
        setStartupStep("Loading");
        setQueue(await backend.buildState());
        // A Mac that is not set up yet opens on Setup.
        const setup = await backend.setupStatus();
        setSetupLeft(setup.steps.filter((s) => s.state !== "done").length);
        if (!setup.complete) setScreen("setup");
        await subscribe(events.onBuildLog((l) => setLog((prev) => (prev.length > 5000 ? [...prev.slice(-4000), l] : [...prev, l]))));
        await subscribe(events.onBuildState(setQueue));
        await subscribe(events.onInstallRequest(handleInstallRequest));
        await subscribe(events.onHostAdded(setAdded));
        await subscribe(
          events.onConfirmRequest((r) => {
            setConfirm(r);
            setScreen("build");
          }),
        );
        if (!active) return;
        // Links that arrived before we were listening (including the launch link).
        for (const r of await backend.takePendingInstalls()) handleInstallRequest(r);
        // A confirmation the worker was already waiting for (after a page reload).
        setConfirm(await backend.pendingConfirm());
      } catch (e) {
        setFatal(errorMessage(e));
      }
      void refreshChecks();
    })();
    return () => {
      active = false;
      for (const u of unlisten) u();
      unlisten.length = 0;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Run a pending link install once signed in.
  useEffect(() => {
    if (auth?.signedIn && pendingInstall) {
      const r = pendingInstall;
      setPendingInstall(null);
      startInstall(r.project, r.version, null, "link");
    }
  }, [auth, pendingInstall, startInstall]);

  // When a new job starts, show only its lines.
  const currentJobId = queue.current?.id ?? null;
  const seenJob = useRef<number | null>(null);
  useEffect(() => {
    if (currentJobId !== null && currentJobId !== seenJob.current) {
      seenJob.current = currentJobId;
      setLog((prev) => prev.filter((l) => l.jobId === currentJobId));
    }
  }, [currentJobId]);

  async function onSignOut() {
    try {
      await backend.signOut();
    } catch (e) {
      setFatal(errorMessage(e));
    }
    setSessionNote(null);
    await refreshAuth();
    setScreen("projects");
  }

  async function onSettingsSaved(s: SettingsType) {
    setSettings(s);
    setSessionNote(null);
    await refreshAuth();
  }

  if (fatal) {
    return (
      <div className="main">
        <div className="inner">
          <div className="notice bad selectable">{fatal}</div>
          <button onClick={() => window.location.reload()}>Reload</button>
        </div>
      </div>
    );
  }

  if (!settings || !auth) {
    return (
      <div className="main">
        <p className="muted">{startupStep}…</p>
      </div>
    );
  }

  const canBuild = checks.length === 0 || checks.some((c) => c.id === "shell" && c.status === "ok");
  const signedIn = auth.signedIn;
  // Signed out, every screen but Setup and Settings asks to sign in first.
  const effectiveScreen: Screen = !signedIn && screen !== "settings" && screen !== "setup" ? "signin" : screen;
  const signInFrom = (from: Screen) => {
    setAfterSignIn(from);
    setScreen("signin");
  };

  return (
    <div className="app">
      {confirm && (
        <ConfirmInstall
          request={confirm}
          onAnswer={(ok) => {
            const jobId = confirm.jobId;
            setConfirm(null);
            backend.confirmInstall(jobId, ok).catch((e) => setFatal(errorMessage(e)));
          }}
        />
      )}
      <div className="topbar">
        <span className="brand">
          <span className="dot" /> Pangea
        </span>
        <button className={`tab ${effectiveScreen === "setup" ? "active" : ""}`} onClick={() => setScreen("setup")}>
          Setup
          {setupLeft > 0 && <span className="count">{setupLeft}</span>}
        </button>
        {signedIn && (
          <>
            <button className={`tab ${effectiveScreen === "projects" ? "active" : ""}`} onClick={() => setScreen("projects")}>
              Projects
            </button>
            <button className={`tab ${effectiveScreen === "build" ? "active" : ""}`} onClick={() => setScreen("build")}>
              Build
              {queue.current && <span className="count">{1 + queue.queue.length}</span>}
            </button>
          </>
        )}
        <button className={`tab ${effectiveScreen === "settings" ? "active" : ""}`} onClick={() => setScreen("settings")}>
          Settings
        </button>
        <span className="spacer" />
        {signedIn && <span className="who">{auth.email ?? auth.apiBaseUrl}</span>}
        {signedIn && (
          <button className="quiet" onClick={onSignOut}>
            Sign out
          </button>
        )}
      </div>

      <div className="main">
        <div className="inner">
          {added && (
            <div className={`notice ${added.ok ? "good" : "bad"} selectable`} role="status">
              {added.message}{" "}
              <button className="quiet" onClick={() => setAdded(null)}>
                Close
              </button>
            </div>
          )}
          {effectiveScreen === "signin" && (
            <SignIn
              apiBaseUrl={settings.apiBaseUrl}
              pendingInstall={pendingInstall}
              initialError={sessionNote ?? auth.error}
              onSignedIn={(a) => {
                setSessionNote(null);
                setAuth(a);
                setScreen(afterSignIn === "signin" ? "projects" : afterSignIn);
                setAfterSignIn("projects");
              }}
              onOpenSettings={() => setScreen("settings")}
            />
          )}
          {effectiveScreen === "setup" && (
            <Setup
              onSignIn={() => signInFrom("setup")}
              onOpenSettings={() => setScreen("settings")}
              onStatus={(s) => {
                const left = s.steps.filter((step) => step.state !== "done").length;
                setSetupLeft(left);
                // The tools may have changed: the lists elsewhere look again.
                if (left === 0) void refreshChecks();
              }}
            />
          )}
          {effectiveScreen === "projects" && (
            <Projects
              checks={checks}
              queue={queue}
              canBuild={canBuild}
              onInstall={(p: Project) => startInstall(p.id, p.headVersion, p.name, "ui")}
              onOpenSettings={() => setScreen("settings")}
              onSessionExpired={() => {
                setSessionNote("Your session has expired. Sign in again.");
                void refreshAuth();
              }}
            />
          )}
          {effectiveScreen === "build" && (
            <Build
              queue={queue}
              log={log}
              onClearHistory={() => {
                backend.clearBuildHistory().then(setQueue).catch(() => undefined);
              }}
            />
          )}
          {effectiveScreen === "settings" && (
            <Settings settings={settings} checks={checks} auth={auth} onSaved={onSettingsSaved} onRecheck={refreshChecks} onSignOut={onSignOut} />
          )}
        </div>
      </div>
    </div>
  );
}
