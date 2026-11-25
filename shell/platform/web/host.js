// Its Felt parts derive from Pattern Player by Lukasz Furman: licensed as shell/platform/web/FELT-LICENSE.md says. Also offered under AGPL-3.0-or-later, at your choice (25 November 2026).
// The web view panel (test): draws the plugin with the component library (pangea.js) and keeps it
// in step with the plugin's parameters through JUCE's native integration (PangeaWebEditor.h).
//
// The editor passes the plugin's controls, its ui.json, the name of the member who built it and
// Pangea's address as initialisation data. Each Faust control becomes a library parameter (a menu
// becomes a choice, a check box a toggle, a button a momentary press) and goes into the section of
// the Faust group that holds it. The five looks map onto a library spec. Changes go to the plugin
// with setParam; a drag is one gesture for the host, ended when the pointer comes up. Changes from
// the host (automation, presets) arrive as "params" events. The footer ("Built by <name> with
// Pangea") is a link: a press asks the editor to open Pangea in the system's browser (openSite,
// which takes no address: the editor keeps it).
//
// Every plugin has one bypass and Pangea supplies it (decisions 74 and 75). In a plugin it is one
// of the plugin's own parameters, and the editor hands this page `bypass: { on }` with the
// controls. So the header bar is drawn, the plugin's name on the left and the bypass button at
// the far right, as the builder's page draws it (builder.js), through the same mapping
// (hostBypass). A press on the button asks the editor to move the parameter (setBypass) and
// changes nothing here. The button, the word "Bypassed" beside it and the panel's fade change
// only when the editor says where the parameter is, with a "bypass" event: after this page's own
// press, and after a change from anywhere else (automation, the host's own bypass button).
import { getNativeFunction } from "./index.js";
//
// The plugin's own meters and step lights (decision 91). The editor hands this page the tree of
// the sound code that runs (`faust: { kind, tree }`), and the page finds the meters and the step
// displays in it by the site's own rules (faustMeters), so it draws what the builder draws for the
// same code: every meter in the section of its Faust group, a left and right pair as one stereo
// meter (decisions 76, 88 and 89). The panel is drawn with drawPanel, as the builder's page draws
// it, so each meter starts at rest and the library's demo animation never runs. The editor sends
// the values as "meters" events, by address, and the same driver moves each meter from its own
// value and from nothing else; a null rests it, and the editor sends null wherever nothing of the
// plugin is heard (bypassed, or the host no longer runs it). A step display's value lights the
// step playing, as the site's does; a MIDI tool's bars light from its module's display slots,
// sent as "display" events.
//
// A planned panel (decision 79) is fitted before it is drawn, as on the builder's page: the panel
// planner's arrangement is made to need one page where it can, at most two above 24 controls, and
// never more than the same plugin's panel with no plan (fitPanel, decision 90). The candidates are
// counted with the spec drawPanel draws, meters included, in an element that is never on the page;
// the panel is drawn once, below. The spec has the mapping's taller page (PAGE_ROWS), and this page
// tells the editor the height drawn, which the plugin's window takes.
import {
  countPages,
  drawnSpec,
  drawPanel,
  faustMeters,
  fitPanel,
  hostBypass,
  linkFooter,
  MOST_PAGES,
  playheadLighter,
  stepSlots,
  toN,
} from "./mapping.js";

const fail = (msg) => {
  const el = document.getElementById("fail");
  el.style.display = "block";
  el.textContent = "Pangea web view: " + msg;
  try { getNativeFunction("report")(el.getBoundingClientRect().height || 120, -1, String(msg)); } catch (_) {}
};
window.addEventListener("error", (e) => fail(e.message || String(e)));

const sent = window.__JUCE__.initialisationData.pangea[0];
const setParam = getNativeFunction("setParam");
const endGestures = getNativeFunction("endGestures");
const report = getNativeFunction("report");
const openSite = getNativeFunction("openSite");
const setBypass = getNativeFunction("setBypass");

// ---------- draw, and keep in step ----------
// The meters and step displays of the sound code, found as the site finds them.
const sound = sent.faust && typeof sent.faust === "object"
  ? faustMeters(sent.faust.tree, sent.faust.kind)
  : { meters: [], playheads: [] };
const data = sound.meters.length ? Object.assign({}, sent, { meters: sound.meters }) : sent;
const { panel, fit } = fitPanel(data, (p) => countPages(Pangea, drawnSpec(p), p.params));
// A planned panel that still needs more than two pages is drawn as the best the fit found, and said
// so (decision 97).
if (fit && fit.pages > MOST_PAGES) console.warn("Pangea: this planned panel needs " + fit.pages + " pages after the fit (" + fit.step + ").");
const { byId } = panel;
const root = document.getElementById("plugin");
const { ctx, meter, stage } = drawPanel(root, panel, null);
linkFooter(root, data.site, () => openSite());
// What the page found and drew, for the plugin's own checks (tools/host-check).
window.__pangeaPanel = panel;
window.__pangeaSound = sound;
window.__pangeaMeter = meter;
// The notes a MIDI tool plays, for the Felt look's chord map and notes (docs/felt-look.md):
// the editor says it will send them (`notes: true`) and sends them as "notes" events, at most 30
// times a second, and "quiet" when the transport stops or the tool is bypassed.
window.__pangeaStage = stage;
window.__JUCE__.backend.addEventListener("notes", (n) => {
  if (n && Array.isArray(n.events)) stage.notes(n.events);
});
window.__JUCE__.backend.addEventListener("quiet", () => stage.quiet());

// The bypass is the plugin's parameter. The header's button shows where the editor last said it
// is, from the start data and then from each "bypass" event, and a press only asks the editor.
let bypassed = panel.bypass !== null && panel.bypass.on;
const showBypass = () => hostBypass(root, bypassed, (on) => setBypass(on === true));
showBypass();
window.__JUCE__.backend.addEventListener("bypass", (b) => {
  bypassed = !!b && b.on === true;
  showBypass();
});

// The step playing now lights its control. A Faust plugin's steps light from its own step
// displays, whose values come with the meters' (decision 89); a MIDI tool's bars from the display
// slots its module writes, which the editor sends as "display" events.
const heads = playheadLighter(root, panel);
window.__pangeaHeads = heads;
const stepsFromSound = sound.playheads.length > 0;
window.__JUCE__.backend.addEventListener("display", (d) => {
  if (!stepsFromSound) heads.show(d && d.slots);
});
let meterEvents = 0;
window.__pangeaMeterEvents = () => meterEvents;
window.__JUCE__.backend.addEventListener("meters", (m) => {
  meterEvents += 1;
  const values = m && typeof m === "object" ? m.values : null;
  window.__pangeaMeterValues = values;
  meter.show(values);
  if (stepsFromSound) heads.show(stepSlots(sound.playheads, values));
});

let applying = false;
const apply = (values) => {
  applying = true;
  try {
    for (const id in values) if (byId[id]) ctx.set(id, Math.min(1, Math.max(0, toN(byId[id], values[id]))));
  } finally { applying = false; }
};
apply(data.values);

let dragging = false;
root.addEventListener("pointerdown", () => { dragging = true; }, true);
const release = () => { if (dragging) { dragging = false; endGestures(); } };
window.addEventListener("pointerup", release, true);
window.addEventListener("pointercancel", release, true);
window.addEventListener("blur", release);

root.addEventListener("pangea:change", (e) => {
  if (applying) return;
  const p = byId[e.detail.id];
  if (!p) return;
  let v = e.detail.value;
  if (p.values) v = p.values[Math.round(v)];
  setParam(e.detail.id, v, dragging);
});
window.__JUCE__.backend.addEventListener("params", (values) => apply(values));

// Tell the editor how tall the plugin is at this width, now and whenever it changes.
let lastH = 0;
const tell = () => {
  const h = Math.ceil(root.getBoundingClientRect().height);
  if (h && h !== lastH) { lastH = h; report(h, performance.now(), ""); }
};
requestAnimationFrame(() => requestAnimationFrame(tell));
new ResizeObserver(tell).observe(root);
// A page brought back as it was left (the web view shows a blank page while the editor is hidden,
// and comes back to this one) says so again: the editor then tells it where the bypass is now, and
// what the meters read.
// Its stage ends the notes it was holding: their offs went to the blank page meanwhile, and the
// chord map and the roll would otherwise hold them for ever (the review of 19 November).
window.addEventListener("pageshow", (e) => {
  if (e.persisted) { lastH = 0; tell(); stage.quiet(); }
});
