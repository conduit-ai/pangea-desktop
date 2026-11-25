import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// Tauri expects a fixed port and does not want the screen cleared while it prints Rust output.
const host = process.env.TAURI_DEV_HOST;

export default defineConfig({
  plugins: [react()],
  clearScreen: false,
  server: {
    port: 1420,
    strictPort: true,
    host: host || false,
    watch: {
      ignored: ["**/src-tauri/**", "**/mock-server/**"],
    },
  },
  envPrefix: ["VITE_", "TAURI_ENV_*"],
  // An inline PostCSS config stops Vite walking up to the repository root's
  // postcss.config.mjs (the web app's Tailwind setup), which is not ours.
  css: {
    postcss: { plugins: [] },
  },
  build: {
    target: "safari16",
    sourcemap: false,
  },
});
