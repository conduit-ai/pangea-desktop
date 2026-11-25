// Shapes shared with the Rust side (serde, camelCase). Keep in step with src-tauri/src.

export type Settings = {
  apiBaseUrl: string;
  /** A developer's own shell folder, used in place of the app's own copy; empty for the app's. */
  shellDir: string;
};

/** Which shell builds use (src-tauri/src/settings.rs, shell_info). */
export type ShellInfo = {
  source: "own" | "app" | "none";
  dir: string;
  version: string | null;
};

export type CheckStatus = "ok" | "warn" | "missing";

export type Check = {
  id: string;
  name: string;
  status: CheckStatus;
  detail: string;
  fix: string | null;
};

/** Apple's build tools (src-tauri/src/apple.rs): there, and able to build a program. */
export type AppleTools = {
  state: "ready" | "missing" | "installing" | "stopped" | "licence" | "broken";
  message: string;
  developerDir: string | null;
  developerDirOverride: string | null;
  sdk: string | null;
  compiler: string | null;
  details: string;
  estimate: string | null;
  helpUrl: string | null;
};

/** What builds still need fetched (src-tauri/src/tools.rs). */
export type ToolNeeds = {
  cmake: boolean;
  juce: boolean;
  juceInOwnShellMissing: boolean;
};

/** One step of fetching CMake or JUCE. */
export type ToolFetch = {
  tool: string;
  version: string;
  stage: "download" | "check" | "unpack" | "ready" | "failed";
  received: number;
  total: number;
  message: string;
};

/** The Pangea plugin on this Mac (src-tauri/src/host_install.rs). */
export type HostPlugin = {
  state: "missing" | "partial" | "installed" | "check-failed" | "update-needed" | "update-available";
  message: string;
  installedVersion: string | null;
  wantedVersion: string | null;
  byApp: boolean;
};

export type HostPluginStatus = {
  plugin: HostPlugin;
  /** The DAWs open now, by name. */
  daws: string[];
  installing: boolean;
};

/** What an install of the Pangea plugin did. */
export type HostInstallReport = {
  ok: boolean;
  /** `daw` (stopped before anything: a DAW is open), `build`, `install`, `registrar`, `auval` or `done`. */
  stage: string;
  message: string;
  auval: [string, string][];
  seconds: number;
};

/** The self-check at the end of Setup (src-tauri/src/selfcheck.rs). */
export type CheckReport = {
  ok: boolean;
  stage: string;
  message: string;
  seconds: number;
};

/** One button of the Setup screen (src-tauri/src/setup.rs). */
export type SetupAction = {
  id:
    | "install-apple"
    | "open-xcode"
    | "check-apple"
    | "sign-in"
    | "get-tools"
    | "open-settings"
    | "install-plugin"
    | "install-plugin-anyway"
    | "run-check"
    | "unpack-again";
  label: string;
};

export type SetupStep = {
  id: "apple" | "signin" | "tools" | "plugin" | "check";
  title: string;
  state: "done" | "doing" | "needs-you" | "failed" | "waiting";
  detail: string;
  estimate: string | null;
  action: SetupAction | null;
  extra: SetupAction | null;
  /** A page to open, such as Apple's download page when Apple's installer stopped. */
  link: { label: string; url: string } | null;
  /** What Copy details adds: the compiler's own words, an error in full. */
  details: string;
  /** The check step only: the later tick in the DAW, apart from the button's own check. */
  daw: { state: "done" | "waiting"; text: string } | null;
};

export type SetupStatus = {
  steps: SetupStep[];
  complete: boolean;
  /** What the main button does. */
  next: SetupAction | null;
  busy: boolean;
  dawSeen: string | null;
};

/** The app's version and the tools it carries or fetches (src-tauri/src/toolchain.rs). */
export type About = {
  appVersion: string;
  commit: string | null;
  carriesTools: boolean;
  faust: string;
  ninja: string;
  cmake: string;
  juce: string;
  licencesDir: string | null;
};

export type AuthStatus = {
  signedIn: boolean;
  email: string | null;
  name: string | null;
  apiBaseUrl: string;
  error: string | null;
};

export type DeviceStart = {
  deviceCode: string;
  userCode: string;
  verificationUri: string;
  verificationUriComplete: string;
  expiresIn: number;
  interval: number;
};

export type Project = {
  id: string;
  name: string;
  slug: string;
  kind: string;
  visibility: string;
  headVersion: number | null;
  owner: string;
  ownerDetails: { id: string | null; name: string | null; email: string | null };
  description: string | null;
  updatedAt: string | null;
  /**
   * Where the plugin is filed (decision 85): the category's id and the name people read,
   * its tags, the other words it is found by as its category (searched, never shown) and the head version's own
   * summary. Null and empty for a plugin that is not filed.
   */
  category: string | null;
  categoryName: string | null;
  tags: string[];
  keywords: string[];
  summary: string | null;
};

export type InstallJob = {
  id: number;
  projectId: string;
  version: number | null;
  projectName: string | null;
  source: "ui" | "link";
  queuedAt: string;
};

export type JobResult = {
  id: number;
  projectId: string;
  projectName: string;
  version: number | null;
  ok: boolean;
  outcome: "installed" | "failed" | "refused" | "cancelled";
  message: string;
  log: string;
  component: string | null;
  /** A MIDI tool's second plugin, the AU MIDI effect for Logic ("<name> FX.component"). */
  componentFx: string | null;
  vst3: string | null;
  seconds: number | null;
  buildId: string | null;
  projectDir: string | null;
  finishedAt: string;
};

export type QueueState = {
  current: InstallJob | null;
  queue: InstallJob[];
  history: JobResult[];
};

export type LogLine = {
  jobId: number;
  stream: "stdout" | "stderr" | "app";
  line: string;
};

export type InstallRequest = {
  project: string;
  version: number | null;
  url: string;
};

export type ConfirmRequest = {
  jobId: number;
  projectName: string;
  version: number;
  owner: string;
  ownedByMe: boolean;
  fromLink: boolean;
  hasLogic: boolean;
  /** `effect`, `instrument` or `midi` (a MIDI tool). */
  kind: string;
};

/** After an add link from the site (decision 87): whether the version is ready in the Pangea plugin. */
export interface HostAdded {
  ok: boolean;
  message: string;
}
