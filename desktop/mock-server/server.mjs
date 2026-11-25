#!/usr/bin/env node
// A tiny stand-in for the Pangea web app, so the desktop app can be tried end to
// end without the web side. No dependencies. Fixed data: three projects, an effect,
// an instrument and a MIDI tool (the shell's midi-steps fixture, docs/midi-tools.md). The
// first two are filed under a category (decision 85) and the third is not.
// Run: node desktop/mock-server/server.mjs  (port 3000, or PORT). It listens on 127.0.0.1 only: its
// approve route checks no session, so nothing beyond this Mac may reach it.
//
// It implements:
//   POST /api/auth/device/code      Better Auth device flow (JSON or form body)
//   POST /api/auth/device/token     grant_type urn:ietf:params:oauth:grant-type:device_code
//   GET  /device?user_code=...      the approval page (Approve / Deny buttons)
//   POST /api/auth/device/approve   { userCode }   (no session check in the mock)
//   POST /api/auth/device/deny      { userCode }
//   GET  /api/desktop/me
//   GET  /api/desktop/projects
//   GET  /api/desktop/projects/:id/versions/:number
//   POST /api/desktop/builds
//   PATCH /api/desktop/builds/:id
//   GET  /projects/:id              a placeholder project page
//
// Everything printed to the console is what the desktop app sent.

import http from "node:http";
import { randomBytes } from "node:crypto";
import { readFileSync } from "node:fs";

const PORT = Number(process.env.PORT || 3000);
const BASE = process.env.BASE_URL || `http://localhost:${PORT}`;
const AUTO_APPROVE_MS = process.env.MOCK_AUTO_APPROVE_MS ? Number(process.env.MOCK_AUTO_APPROVE_MS) : 0;
const INTERVAL = 2; // seconds between polls; Better Auth defaults to 5
const EXPIRES_IN = 300;

const user = { id: "u1", email: "ada@example.com", name: "Ada" };

// Project ids are UUIDs, as in the web app; the desktop app refuses anything else.
const P1 = "3f2a1c4e-9b7d-4e6a-8c1f-0a2b3c4d5e6f";
const P2 = "9d8c7b6a-5f4e-4d3c-8b2a-1f0e9d8c7b6a";
const P3 = "5b1d7e2a-3c4f-4a6b-9d8e-7f6a5b4c3d2e";

const projects = [
  {
    id: P1,
    name: "Warm Tape Delay",
    slug: "warm-tape-delay",
    kind: "effect",
    visibility: "public",
    description: "A tape style delay with wow, flutter and a soft saturation stage.",
    // Where it is filed (decision 85), as the web side sends it.
    category: "delay",
    categoryName: "Delay",
    tags: ["tape", "saturation", "tempo-synced"],
    keywords: ["echo"],
    summary: "A tape style echo whose repeats darken and wobble. Use it for dub throws.",
    headVersion: 3,
    owner: { id: "u1", name: "Ada", handle: "ada" },
    pluginCode: "Pwtd",
    updatedAt: "2026-11-06T22:10:00Z",
  },
  {
    id: P2,
    name: "Acid Bass",
    slug: "acid-bass",
    kind: "instrument",
    visibility: "private",
    description: "A squelchy monosynth with a resonant filter and an accent control.",
    category: "mono-synth",
    categoryName: "Mono synth",
    tags: ["bass", "subtractive", "ladder"],
    keywords: ["monosynth", "monophonic"],
    summary: "A squelchy bass voice with a resonant filter and an accent.",
    headVersion: 1,
    owner: { id: "u2", name: "Sam", handle: "sam" },
    pluginCode: "Pacb",
    updatedAt: "2026-11-07T00:40:00Z",
  },
  {
    id: P3,
    name: "MIDI Steps",
    slug: "midi-steps",
    kind: "midi",
    visibility: "private",
    description: "A four-step arpeggio with accent bars, played into another instrument.",
    // Not filed: a plugin made before decision 85 has none of the five fields.
    headVersion: 1,
    owner: { id: "u1", name: "Ada", handle: "ada" },
    pluginCode: "Mstp",
    fxPluginCode: "Mstx",
    updatedAt: "2026-11-07T23:30:00Z",
  },
];

// The MIDI tool is the shell's own fixture, so a real build from the mock builds the real thing.
const fixture = new URL("../../shell/examples/midi-steps/", import.meta.url);
const faustMidiTool = readFileSync(new URL("dsp.dsp", fixture), "utf8");
const logicMidiTool = readFileSync(new URL("logic.cpp", fixture), "utf8");

const faustEffect = `import("stdfaust.lib");
declare name "Warm Tape Delay";
time = hslider("Time [unit:ms][style:knob]", 350, 20, 1200, 1) : si.smoo;
feedback = hslider("Feedback [style:knob]", 0.4, 0, 0.95, 0.01) : si.smoo;
mix = hslider("Mix [style:knob]", 0.3, 0, 1, 0.01) : si.smoo;
delayLine(x) = x + (x : de.fdelay(65536, time * ma.SR / 1000)) * feedback;
channel(x) = x * (1 - mix) + (x : delayLine : ma.tanh) * mix;
process = channel, channel : par(i, 2, min(1.0) : max(-1.0));
`;

const faustInstrument = `import("stdfaust.lib");
declare name "Acid Bass";
declare options "[nvoices:8]";
freq = hslider("freq", 110, 20, 2000, 0.01);
gain = hslider("gain", 0.5, 0, 1, 0.01);
gate = button("gate");
cutoff = hslider("Cutoff [unit:Hz][scale:log][style:knob]", 800, 80, 8000, 1) : si.smoo;
res = hslider("Resonance [style:knob]", 0.6, 0, 0.95, 0.01) : si.smoo;
env = en.adsr(0.005, 0.2, 0.3, 0.1, gate);
voice = os.sawtooth(freq) * env * gain : ve.moog_vcf(res, cutoff * (0.5 + env));
process = voice <: _, _ : par(i, 2, min(1.0) : max(-1.0));
`;

// The keys shell/tools/read-plugin-json.py reads: name, slug, kind, bundleId, manufacturerCode,
// pluginCode, version (a whole version number becomes 0.<n>.0), description.
function pluginJson(p, number) {
  return {
    name: p.name,
    slug: p.slug,
    kind: p.kind,
    description: p.description,
    projectId: p.id,
    version: number,
    faustVersion: "2.90.19",
    bundleId: `com.pangea.${p.slug}`,
    manufacturerCode: "Pnga",
    pluginCode: p.pluginCode,
    // A MIDI tool is two plugins; the second, the AU MIDI effect, has its own code.
    ...(p.kind === "midi" ? { fxPluginCode: p.fxPluginCode } : {}),
  };
}

// Version 1 of the effect reads a file with `soundfile`, which the desktop app's
// safety check must refuse. Try it with:
//   open "pangea://install?project=3f2a1c4e-9b7d-4e6a-8c1f-0a2b3c4d5e6f&version=1"
const faustUnsafe = `import("stdfaust.lib");
declare name "Warm Tape Delay";
ir = soundfile("impulse [url:{'/etc/passwd'}]", 2);
process = _, _;
`;

function versionBundle(p, number) {
  const plugin = pluginJson(p, number);
  plugin.owner = { id: p.owner.id, name: p.owner.name };
  return {
    versionId: `${p.id}-v${number}`,
    plugin,
    faust:
      p.kind === "midi"
        ? faustMidiTool
        : p.kind === "effect"
          ? number === 1
            ? faustUnsafe
            : faustEffect
          : faustInstrument,
    logic: p.kind === "midi" ? logicMidiTool : null,
    ui: { title: p.name, palette: p.kind === "effect" ? "amber" : "teal", knobStyle: "round" },
  };
}

// ---- state ------------------------------------------------------------------

const deviceCodes = new Map(); // device_code -> { userCode, status, expiresAt, lastPolledAt, clientId }
const byUserCode = new Map(); // user_code -> device_code
const tokens = new Set();
const builds = new Map();
let buildCounter = 0;

const alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
function userCode() {
  const bytes = randomBytes(8);
  return Array.from(bytes, (b) => alphabet[b % 32]).join("");
}

// ---- helpers ----------------------------------------------------------------

function send(res, status, body, headers = {}) {
  const text = typeof body === "string" ? body : JSON.stringify(body);
  res.writeHead(status, {
    "Content-Type": typeof body === "string" ? "text/html; charset=utf-8" : "application/json",
    "Cache-Control": "no-store",
    ...headers,
  });
  res.end(text);
}

function oauthError(res, status, error, description) {
  send(res, status, { error, error_description: description });
}

async function readBody(req) {
  const chunks = [];
  for await (const c of req) chunks.push(c);
  const raw = Buffer.concat(chunks).toString("utf8");
  const type = (req.headers["content-type"] || "").toLowerCase();
  if (!raw) return {};
  if (type.includes("application/x-www-form-urlencoded")) {
    return Object.fromEntries(new URLSearchParams(raw));
  }
  try {
    return JSON.parse(raw);
  } catch {
    return Object.fromEntries(new URLSearchParams(raw));
  }
}

function bearer(req) {
  const h = req.headers.authorization || "";
  const m = /^Bearer\s+(.+)$/i.exec(h);
  return m ? m[1].trim() : null;
}

function requireAuth(req, res) {
  const t = bearer(req);
  if (!t || !tokens.has(t)) {
    send(res, 401, { message: "Unauthorized" });
    return false;
  }
  return true;
}

function log(...args) {
  console.log(new Date().toISOString().slice(11, 19), ...args);
}

// ---- pages ------------------------------------------------------------------

function devicePage(code, rec) {
  const status = rec ? rec.status : "unknown";
  const auto = AUTO_APPROVE_MS > 0 && rec && rec.status === "pending";
  return `<!doctype html><html><head><meta charset="utf-8"><title>Pangea mock: approve device</title>
<style>body{font-family:-apple-system,system-ui,sans-serif;background:#141517;color:#e6e6e6;display:grid;place-items:center;height:100vh;margin:0}
main{background:#1c1e22;border:1px solid #2a2d33;border-radius:12px;padding:32px;max-width:420px;text-align:center}
code{font-size:28px;letter-spacing:4px;display:block;margin:16px 0;color:#f0b35c}
button{font-size:15px;padding:10px 18px;border-radius:8px;border:1px solid #2a2d33;background:#2a2d33;color:#fff;margin:4px;cursor:pointer}
button.primary{background:#f0b35c;color:#141517;border-color:#f0b35c}
p.muted{color:#9a9fa8;font-size:14px}</style></head><body><main>
<h1 style="font-size:20px;margin:0">Pangea (mock)</h1>
<p>Approve the Pangea desktop app to use your account?</p>
<code>${code || "no code"}</code>
<p class="muted">Signed in as ${user.email}. Status: <b id="s">${status}</b></p>
${rec && rec.status === "pending" ? `<button class="primary" onclick="act('approve')">Approve</button><button onclick="act('deny')">Deny</button>` : ""}
${auto ? `<p class="muted">Auto approving in ${AUTO_APPROVE_MS} ms (MOCK_AUTO_APPROVE_MS).</p>` : ""}
<script>
async function act(what){const r=await fetch('/api/auth/device/'+what,{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify({userCode:${JSON.stringify(code || "")}})});const j=await r.json();document.getElementById('s').textContent=j.success?(what==='approve'?'approved. You can close this tab.':'denied'):JSON.stringify(j);}
</script></main></body></html>`;
}

function projectPage(p) {
  return `<!doctype html><html><head><meta charset="utf-8"><title>${p.name} (mock)</title>
<style>body{font-family:-apple-system,system-ui,sans-serif;background:#141517;color:#e6e6e6;padding:40px}a{color:#f0b35c}</style></head>
<body><h1>${p.name}</h1><p>${p.description}</p><p>${p.kind}, v${p.headVersion}, ${p.visibility}, by ${p.owner.name}</p>
<p><a href="pangea://install?project=${p.id}&version=${p.headVersion}">Install in Logic</a> (opens the desktop app)</p>
<p style="color:#9a9fa8">This is the mock server's placeholder for the web app's project page.</p></body></html>`;
}

// ---- routes -----------------------------------------------------------------

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, BASE);
  const path = url.pathname.replace(/\/+$/, "") || "/";
  const method = req.method.toUpperCase();

  try {
    // --- device flow
    if (method === "POST" && path === "/api/auth/device/code") {
      const body = await readBody(req);
      if (!body.client_id) return oauthError(res, 400, "invalid_request", "client_id is required");
      const device_code = randomBytes(20).toString("hex");
      const user_code = userCode();
      deviceCodes.set(device_code, {
        userCode: user_code,
        status: "pending",
        expiresAt: Date.now() + EXPIRES_IN * 1000,
        lastPolledAt: 0,
        clientId: body.client_id,
        scope: body.scope || "",
      });
      byUserCode.set(user_code, device_code);
      log("device/code", body.client_id, "->", user_code);
      if (AUTO_APPROVE_MS > 0) {
        setTimeout(() => {
          const rec = deviceCodes.get(device_code);
          if (rec && rec.status === "pending") {
            rec.status = "approved";
            log("auto approved", user_code);
          }
        }, AUTO_APPROVE_MS);
      }
      return send(res, 200, {
        device_code,
        user_code,
        verification_uri: `${BASE}/device`,
        verification_uri_complete: `${BASE}/device?user_code=${user_code}`,
        expires_in: EXPIRES_IN,
        interval: INTERVAL,
      });
    }

    if (method === "POST" && path === "/api/auth/device/token") {
      const body = await readBody(req);
      if (body.grant_type !== "urn:ietf:params:oauth:grant-type:device_code") {
        return oauthError(res, 400, "invalid_request", "unsupported grant_type");
      }
      const rec = deviceCodes.get(body.device_code);
      if (!rec) return oauthError(res, 400, "invalid_grant", "Invalid device code");
      if (rec.clientId !== body.client_id) return oauthError(res, 400, "invalid_grant", "Client ID mismatch");
      const now = Date.now();
      if (now - rec.lastPolledAt < INTERVAL * 1000) {
        rec.lastPolledAt = now;
        log("device/token slow_down", rec.userCode);
        return oauthError(res, 400, "slow_down", "Polling too frequently");
      }
      rec.lastPolledAt = now;
      if (rec.expiresAt < now) {
        deviceCodes.delete(body.device_code);
        return oauthError(res, 400, "expired_token", "Device code has expired");
      }
      if (rec.status === "pending") {
        log("device/token pending", rec.userCode);
        return oauthError(res, 400, "authorization_pending", "Authorization pending");
      }
      if (rec.status === "denied") {
        deviceCodes.delete(body.device_code);
        return oauthError(res, 400, "access_denied", "Access denied");
      }
      deviceCodes.delete(body.device_code);
      byUserCode.delete(rec.userCode);
      const access_token = "mock_" + randomBytes(24).toString("hex");
      tokens.add(access_token);
      log("device/token approved ->", access_token.slice(0, 12) + "...");
      return send(res, 200, { access_token, token_type: "Bearer", expires_in: 60 * 60 * 24 * 7, scope: rec.scope });
    }

    if (method === "GET" && path === "/device") {
      const code = (url.searchParams.get("user_code") || "").toUpperCase();
      const dc = byUserCode.get(code);
      return send(res, 200, devicePage(code, dc ? deviceCodes.get(dc) : null));
    }

    if (method === "POST" && (path === "/api/auth/device/approve" || path === "/api/auth/device/deny")) {
      const body = await readBody(req);
      const code = String(body.userCode || "").toUpperCase();
      const dc = byUserCode.get(code);
      const rec = dc && deviceCodes.get(dc);
      if (!rec) return send(res, 400, { error: "invalid_request", error_description: "Invalid user code" });
      if (rec.status !== "pending") return send(res, 400, { error: "invalid_request", error_description: "Device code already processed" });
      rec.status = path.endsWith("approve") ? "approved" : "denied";
      rec.userId = user.id;
      log(path.endsWith("approve") ? "approved" : "denied", code);
      return send(res, 200, { success: true });
    }

    // --- desktop API
    if (path === "/api/desktop/me" && method === "GET") {
      if (!requireAuth(req, res)) return;
      return send(res, 200, user);
    }

    if (path === "/api/desktop/projects" && method === "GET") {
      if (!requireAuth(req, res)) return;
      log("projects listed");
      return send(res, 200, { projects });
    }

    let m = /^\/api\/desktop\/projects\/([^/]+)\/versions\/(\d+)$/.exec(path);
    if (m && method === "GET") {
      if (!requireAuth(req, res)) return;
      const p = projects.find((x) => x.id === m[1]);
      const n = Number(m[2]);
      if (!p || n < 1 || n > p.headVersion) return send(res, 404, { message: "Version not found" });
      log("version", p.id, "v" + n);
      return send(res, 200, versionBundle(p, n));
    }

    if (path === "/api/desktop/builds" && method === "POST") {
      if (!requireAuth(req, res)) return;
      const body = await readBody(req);
      const id = `b${++buildCounter}`;
      builds.set(id, { id, ...body, status: "queued", createdAt: new Date().toISOString() });
      log("build created", id, JSON.stringify(body));
      return send(res, 201, { id });
    }

    m = /^\/api\/desktop\/builds\/([^/]+)$/.exec(path);
    if (m && method === "PATCH") {
      if (!requireAuth(req, res)) return;
      const b = builds.get(m[1]);
      if (!b) return send(res, 404, { message: "Build not found" });
      const body = await readBody(req);
      Object.assign(b, body);
      log(
        "build patched",
        b.id,
        "status=" + body.status,
        body.log != null ? `log=${body.log.length} chars` : "",
        body.faustVersion ? `faust=${body.faustVersion}` : "",
        body.shellVersion ? `shell=${body.shellVersion}` : "",
      );
      return send(res, 200, { ok: true, build: b });
    }
    if (m && method === "GET") {
      if (!requireAuth(req, res)) return;
      const b = builds.get(m[1]);
      return b ? send(res, 200, b) : send(res, 404, { message: "Build not found" });
    }

    // --- placeholder pages
    m = /^\/projects\/([^/]+)$/.exec(path);
    if (m && method === "GET") {
      const p = projects.find((x) => x.id === m[1]);
      return p ? send(res, 200, projectPage(p)) : send(res, 404, "<h1>Not found</h1>");
    }

    if (path === "/" && method === "GET") {
      return send(res, 200, `<!doctype html><title>Pangea mock</title><body style="font-family:system-ui;background:#141517;color:#e6e6e6;padding:40px"><h1>Pangea mock server</h1><p>Projects: ${projects.map((p) => `<a style="color:#f0b35c" href="/projects/${p.id}">${p.name}</a>`).join(", ")}</p></body>`);
    }

    send(res, 404, { message: `No route for ${method} ${path}` });
  } catch (e) {
    console.error(e);
    send(res, 500, { message: String(e) });
  }
});

server.listen(PORT, "127.0.0.1", () => {
  log(`Pangea mock server on ${BASE}`);
  log(`projects: ${projects.map((p) => `${p.name} = ${p.id}`).join(", ")}`);
  if (AUTO_APPROVE_MS > 0) log(`device codes auto approve after ${AUTO_APPROVE_MS} ms`);
});
