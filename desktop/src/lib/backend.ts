// Typed wrappers around the Rust commands and events.

import { invoke } from "@tauri-apps/api/core";
import { listen, type UnlistenFn } from "@tauri-apps/api/event";
import type {
  About,
  AppleTools,
  AuthStatus,
  Check,
  ConfirmRequest,
  DeviceStart,
  InstallJob,
  InstallRequest,
  LogLine,
  Project,
  QueueState,
  Settings,
  ShellInfo,
  HostAdded,
  ToolFetch,
  ToolNeeds,
  HostInstallReport,
  HostPluginStatus,
  CheckReport,
  SetupStatus,
} from "./types";

export const backend = {
  getSettings: () => invoke<Settings>("get_settings"),
  defaultSettings: () => invoke<Settings>("default_settings"),
  setSettings: (settings: Settings) => invoke<Settings>("set_settings", { settings }),
  checkPrerequisites: () => invoke<Check[]>("check_prerequisites"),
  appleTools: () => invoke<AppleTools>("apple_tools"),
  appleToolsInstall: () => invoke<AppleTools>("apple_tools_install"),
  openXcode: () => invoke<void>("open_xcode"),
  about: () => invoke<About>("about"),
  openLicences: () => invoke<void>("open_licences"),
  projectsDir: () => invoke<string>("projects_dir"),
  shellInfo: () => invoke<ShellInfo>("shell_info"),
  getTools: () => invoke<ToolNeeds>("get_tools"),
  toolsNeeds: () => invoke<[ToolNeeds, ToolFetch | null]>("tools_needs"),
  hostPlugin: () => invoke<HostPluginStatus>("host_plugin"),
  installHostPlugin: (anyway: boolean) => invoke<HostInstallReport>("install_host_plugin", { anyway }),
  runSelfCheck: () => invoke<CheckReport>("run_self_check"),
  setupStatus: () => invoke<SetupStatus>("setup_status"),
  retryUnpack: () => invoke<void>("retry_unpack"),

  authStatus: () => invoke<AuthStatus>("auth_status"),
  signInStart: () => invoke<DeviceStart>("sign_in_start"),
  signInWait: (d: DeviceStart) =>
    invoke<AuthStatus>("sign_in_wait", { deviceCode: d.deviceCode, interval: d.interval, expiresIn: d.expiresIn }),
  signInCancel: () => invoke<void>("sign_in_cancel"),
  signOut: () => invoke<void>("sign_out"),

  listProjects: () => invoke<Project[]>("list_projects"),
  openProjectOnWeb: (projectId: string) => invoke<void>("open_project_on_web", { projectId }),
  openUrl: (url: string) => invoke<void>("open_url", { url }),

  buildAndInstall: (projectId: string, version: number | null, projectName: string | null, source: "ui" | "link") =>
    invoke<InstallJob>("build_and_install", { projectId, version, projectName, source }),
  buildState: () => invoke<QueueState>("build_state"),
  clearBuildHistory: () => invoke<QueueState>("clear_build_history"),
  takePendingInstalls: () => invoke<InstallRequest[]>("take_pending_installs"),
  pendingConfirm: () => invoke<ConfirmRequest | null>("pending_confirm"),
  confirmInstall: (jobId: number, ok: boolean) => invoke<boolean>("confirm_install", { jobId, ok }),
};

export const events = {
  onBuildLog: (f: (line: LogLine) => void): Promise<UnlistenFn> => listen<LogLine>("build-log", (e) => f(e.payload)),
  onBuildState: (f: (s: QueueState) => void): Promise<UnlistenFn> =>
    listen<QueueState>("build-state", (e) => f(e.payload)),
  onInstallRequest: (f: (r: InstallRequest) => void): Promise<UnlistenFn> =>
    listen<InstallRequest>("install-request", (e) => f(e.payload)),
  onConfirmRequest: (f: (r: ConfirmRequest) => void): Promise<UnlistenFn> =>
    listen<ConfirmRequest>("confirm-request", (e) => f(e.payload)),
  onHostAdded: (f: (r: HostAdded) => void): Promise<UnlistenFn> =>
    listen<HostAdded>("host-added", (e) => f(e.payload)),
  onToolFetch: (f: (r: ToolFetch) => void): Promise<UnlistenFn> => listen<ToolFetch>("tool-fetch", (e) => f(e.payload)),
  onHostInstallStage: (f: (stage: string) => void): Promise<UnlistenFn> => listen<string>("host-install-stage", (e) => f(e.payload)),
  onHostInstallLog: (f: (line: [string, string]) => void): Promise<UnlistenFn> =>
    listen<[string, string]>("host-install-log", (e) => f(e.payload)),
  onSelfCheckStage: (f: (stage: string) => void): Promise<UnlistenFn> => listen<string>("self-check-stage", (e) => f(e.payload)),
  onSelfCheckLog: (f: (line: [string, string]) => void): Promise<UnlistenFn> =>
    listen<[string, string]>("self-check-log", (e) => f(e.payload)),
};

export function errorMessage(e: unknown): string {
  if (typeof e === "string") return e;
  if (e && typeof e === "object" && "message" in e && typeof (e as { message: unknown }).message === "string") {
    return (e as { message: string }).message;
  }
  return String(e);
}
