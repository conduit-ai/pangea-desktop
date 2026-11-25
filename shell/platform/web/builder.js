// Its Felt parts derive from Pattern Player by Lukasz Furman: licensed as shell/platform/web/FELT-LICENSE.md says. Also offered under AGPL-3.0-or-later, at your choice (25 November 2026).
// The builder's panel on the site: the same page as the plugin's web view (index.html, host.js),
// with the site's page in place of the plugin. It draws with the same library from the same mapping
// (mapping.js), in a frame, so the library's styles and the site's never meet.
//
// The site sends "init" (the controls, their values, the version's ui, the name of the member who
// built the plugin, `bypass: { on }` when the site has a bypass for the plugin, and `meters` when
// the version's sound code declares any), "params" (values that changed elsewhere), "display"
// (the display slots that light the controls with a playhead: a MIDI tool's bars, or a Faust
// plugin's steps, fed by its own bar graphs, decision 89), "edit" (Edit layout on or
// off, and the chosen control), "bypass" (`on`: the site's bypass was switched, and the header's
// button follows with no new drawing) and "meters" (`values`: what each meter reads now, by its
// id, and null for one with no value: see "The meter" below). In Edit layout
// a press sends "select" and moves nothing, except a press on the header's bypass button (below).
// This page sends "ready", "drawn", "change" (a control moved; dragging says whether the pointer
// is still down), "bypass" (`on`: a press on the header's bypass button asks for this; the button
// changes only when the site answers with the same message), "key" (a key press or release this
// page has no use for, passed on to the site's own key listeners: see "The keyboard" below),
// "size" (the panel's height at this width) and "error". Messages go to and come from the page's
// own origin only. The footer ("Built by <name> with Pangea") is a link to this site's first
// page, in a new tab.
//
// The header bar (the name on the left, the bypass button at the far right) is drawn only when
// "init" carries a bypass. The bypass itself is the site's: its test bench switches the plugin out
// of the sound, and this page only shows the state and passes a press on (decision 74). It is
// Pangea's own fixture and no part of the layout, so in Edit layout it stays true and stays live:
// the bar shows the site's state (the button, the word "Bypassed" and the panel's fade), and a
// press on the button asks the site as it does outside Edit layout, and chooses nothing. The name
// and the button are marked for the site's helper (data-help-fixed), as each control is
// (data-pid).
//
// The meters. A version whose sound code declares meters has every one of them on its panel,
// each in the section of its Faust group and a left and right pair as one stereo meter, and they
// show the plugin's own sound (decisions 76 and 88): the site reads the values from the playing
// plugin and sends them, and this page moves each meter from its own and from nothing else.
// Nothing here moves by itself: every drawing is made with drawPanel (mapping.js), which draws
// each meter at rest and stops the library's demo animation, and a meter rests again whenever
// the site sends no value for it, which the site does while nothing of the plugin can be heard.
// A drawing is not a change in the sound, though: a change to the layout draws the panel again
// while the plugin plays on. So a meter that the last drawing had too begins where it stood, and
// does not drop to rest and climb back by itself. Edit layout changes none of this. Each meter
// is marked for the site's helper (data-help-fixed).
//
// A planned panel (decision 79). The panel planner's arrangement is fitted before it is drawn
// (fitPanel, mapping.js; decision 90): its sizes are made smaller, step by step, until it needs
// one page, and for 24 controls or fewer its smallest sections are merged if need be; it may need
// two pages only above 24 controls, and never more than the same plugin's panel with no plan. The
// candidates are drawn where nobody sees them, and only the panel the member sees is drawn here,
// with its meters, its header, its footer link, its control marks and its playheads, as any
// panel is. After a fit,
// "drawn" carries `fit`: the step used, its pages, the most pages the guidelines allow, and the
// arrangement drawn. Edit layout starts from that arrangement, so a person who edits a planned
// panel starts from what they see, and their own sizes are never fitted again. Every "drawn" also
// carries `panel`, what the drawing shows (drawnOf: the family, the pages, the components and
// sizes drawn, the display, and each component the planner preferred that was drawn otherwise),
// which the site keeps with the version so that the variety of panels can be measured
// (docs/panel-planner.md, change 7).
//
// The Felt look (docs/felt-look.md). A MIDI tool's data says `notes: true` when the site will
// send the notes it plays, and then the Felt look draws its chord map and its notes; the site
// sends "notes" (`events`: [on, note, velocity, channel] fours, as they sound) and "quiet" (the
// transport stopped or the tool was bypassed: what sounds has ended). Each drawing's stage reads
// them (stageDriver, mapping.js), and the drawing before it is let go.
import {
  countPages,
  drawnOf,
  drawnSpec,
  drawPanel,
  fitPanel,
  hostBypass,
  linkFooter,
  MOST_PAGES,
  pagesIn,
  playheadLighter,
  toN,
} from "./mapping.js";

const origin = window.location.origin;
const send = (message) => window.parent.postMessage(Object.assign({ pangea: true }, message), origin);
const fail = (message) => send({ type: "error", message: String(message) });
window.addEventListener("error", (e) => fail(e.message || e));

const root = document.getElementById("plugin");
let panel = null, ctx = null, heads = null, meter = null, stage = null, applying = false, dragging = false;

const apply = (values) => {
  if (!panel) return;
  applying = true;
  try {
    for (const id in values) {
      const p = panel.byId[id];
      if (p) ctx.set(id, Math.min(1, Math.max(0, toN(p, values[id]))));
    }
  } finally { applying = false; }
};

root.addEventListener("pointerdown", () => { dragging = true; }, true);
const release = () => { dragging = false; };
window.addEventListener("pointerup", release, true);
window.addEventListener("pointercancel", release, true);
window.addEventListener("blur", release);

root.addEventListener("pangea:change", (e) => {
  if (applying || !panel) return;
  const p = panel.byId[e.detail.id];
  if (!p) return;
  let v = e.detail.value;
  if (p.values) v = p.values[Math.round(v)];
  send({ type: "change", id: e.detail.id, value: v, dragging });
});

let lastH = 0;
const tell = () => {
  const h = Math.ceil(root.getBoundingClientRect().height);
  if (h && h !== lastH) { lastH = h; send({ type: "size", height: h }); }
};
new ResizeObserver(tell).observe(root);

// The library gives a control's element no id, so the site could not tell which control the
// pointer is on (for the helper's tips). Each parameter is moved to an end of its range and back
// while nothing is reported, and the control whose element changed is marked with data-pid. To go
// once the library marks its own controls.
const markControls = () => {
  const seen = new MutationObserver(() => {});
  seen.observe(root, { subtree: true, attributes: true, childList: true, characterData: true });
  applying = true;
  try {
    for (const p of panel.params) {
      const n = ctx.n(p.id);
      seen.takeRecords();
      ctx.set(p.id, n > 0.5 ? 0 : 1);
      const records = seen.takeRecords();
      ctx.set(p.id, n);
      for (const r of records) {
        const at = r.target.nodeType === 1 ? r.target : r.target.parentElement;
        const el = at && at.closest(".pg-ctl");
        // A display (a curve or an envelope) follows the controls it is bound to but is none of
        // them, so it is never marked: it took the mark of the first control it follows, whose
        // own element then had none (17 November 2026: an EQ's curve took Low Freq's, so a click
        // on the Low Freq knob chose nothing in Fine-tune and a click on any dot chose Low Freq).
        if (el && !el.dataset.pid && !el.classList.contains("pg-disp")) { el.dataset.pid = p.id; break; }
      }
    }
  } finally {
    applying = false;
    seen.disconnect();
  }
  markHandles();
};

// Each handle on the panel's display is marked with the control it moves first, so that a click
// on it chooses that control (Fine-tune, Edit layout) and the helper explains it: a curve's dot its
// band's frequency (it moves the gain too), an envelope's A, D and R the attack, the decay (or the
// sustain, when there is no decay) and the release. The display around them stays unmarked.
const markHandles = () => {
  const d = ctx && ctx.S ? ctx.S.display : null;
  const shown = root.querySelector(".pg-disp");
  if (!d || !shown) return;
  const ids =
    d.type === "filter_response" && Array.isArray(d.handles)
      ? d.handles.map((h) => (h && h.x) || null)
      : d.type === "envelope" && d.bind
        ? [d.bind.a, d.bind.d || d.bind.s, d.bind.r]
        : [];
  shown.querySelectorAll(".pg-hand").forEach((hand, i) => {
    if (ids[i]) hand.dataset.pid = ids[i];
  });
};

// Edit layout: a press chooses a control and moves nothing. The site says which one is chosen.
let editing = false;
const editStyle = document.createElement("style");
editStyle.textContent =
  "#plugin[data-editing] [data-pid]{cursor:pointer}" +
  "#plugin[data-editing] [data-pid]:hover{outline:1px dashed #345DDD;outline-offset:4px;border-radius:6px}" +
  "#plugin[data-editing] [data-chosen]{outline:2px solid #345DDD !important;outline-offset:4px;border-radius:6px}";
document.head.append(editStyle);
const choose = (id) => {
  for (const el of root.querySelectorAll("[data-chosen]")) el.removeAttribute("data-chosen");
  if (id == null) return;
  for (const el of root.querySelectorAll("[data-pid]")) if (el.dataset.pid === id) el.setAttribute("data-chosen", "");
};
// The bypass is the site's. This page shows the site's state on the header's button, after every
// drawing and whenever the site says it changed, and a press only asks the site: the button does
// not change until the site answers. Edit layout changes none of this: the bypass is Pangea's own
// fixture, not part of the layout, so the bar shows the true state there too and the button
// works. The button is marked for the site's helper, as markControls marks the controls.
let bypassed = false;
let bypassButton = null;
const askBypass = (on) => send({ type: "bypass", on });
const showBypass = () => {
  bypassButton = hostBypass(root, bypassed, askBypass);
  if (bypassButton) bypassButton.setAttribute("data-help-fixed", "bypass");
};
// Whether an event is on the header's bypass button, or on the sign drawn inside it.
const onBypassButton = (target) =>
  !!bypassButton && !!target && typeof target.closest === "function" &&
  target.closest(".pg-byp") === bypassButton;
// The plugin's name, at the left of the header bar or, for a look with a medium or large title
// (decision 97), on the panel itself, is marked for the site's helper too, with the mark the older
// panel gives its title, so the helper says the same of the name on both panels.
const markName = () => {
  const name = root.querySelector(".pg-hdr-name") || root.querySelector(".pg-title");
  if (name) name.setAttribute("data-help-fixed", "title");
};
const setEditing = (on) => {
  editing = !!on;
  if (editing) root.setAttribute("data-editing", ""); else { root.removeAttribute("data-editing"); choose(null); }
};
for (const type of ["pointerdown", "mousedown", "touchstart", "click", "dblclick", "wheel", "keydown", "contextmenu"]) {
  window.addEventListener(type, (e) => {
    if (!editing) return;
    // The bypass button stays live in Edit layout: its press, by pointer or by keyboard, goes on
    // to the button, which asks the site. It is not stopped here and it chooses nothing, so the
    // control that was chosen stays chosen.
    if (onBypassButton(e.target)) return;
    e.stopPropagation();
    if (type !== "wheel" && !(type === "keydown" && e.key === "Tab")) e.preventDefault();
    if (type === "pointerdown") {
      const el = e.target && e.target.closest ? e.target.closest("[data-pid]") : null;
      send({ type: "select", id: el ? el.dataset.pid : null });
    }
  }, { capture: true, passive: false });
}

// Outside Edit layout, a press on a control, or the keyboard reaching it, says which control it
// is ("press"), so that a side panel such as Fine-tune can show that control on a click, with no
// drag. Nothing is stopped or moved: the press goes on to the control as before.
const tellPress = (target) => {
  if (editing) return;
  const el = target && target.closest ? target.closest("[data-pid]") : null;
  if (el) send({ type: "press", id: el.dataset.pid });
};
root.addEventListener("pointerdown", (e) => tellPress(e.target), true);
root.addEventListener("focusin", (e) => tellPress(e.target));

// The keyboard. A press inside this page takes the keyboard from the site's page, where the
// on-screen keyboard that plays an instrument and the site's other key commands listen. So each
// key this page has no use for is passed on to the site as "key", its press and its release, and
// the site hands it to those same listeners (library-panel.tsx). Nothing here stops a key or
// prevents what it does: the controls get every key as before, and Edit layout keeps its rules.
//
// A press is kept, and nothing is passed on, when it is typed into a field (the box that opens
// to type a value), when Command, Control or Alt is held (left to the browser, as the site's own
// keyboard leaves them), or when it is one of OWN_KEYS: the keys the library's controls use
// (pangea.js: the arrows, and Enter and Space on a switch), the browser's own for a button, a
// link and the focus (Enter, Space, Tab), and Shift itself, which the controls read with an
// arrow and with a drag. A letter typed with Shift held is still passed on, and says so. When
// the library gives a control another key, it goes in OWN_KEYS. A key held down repeats, and a
// repeat is never passed on, so one press starts one note.
//
// A release follows its press. The release of a press this page kept is kept. Every other
// release is passed on: of a press that was passed on, and of a press this page never heard,
// which is a key that went down on the site's page before a press on the panel brought the
// keyboard here. The site must hear that release, or its note would never end. A key that went
// down here and comes up on the site's page is heard there with no help from this page, so the
// presses this page kept are forgotten when the keyboard leaves it.
const OWN_KEYS = ["Tab", "Enter", " ", "ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight", "Shift"];
// Whether a key is typed into a field: an input, a text area or editable text, as the site's
// own keyboard reads it (src/components/bench/keyboard.tsx).
const inField = (target) =>
  !!target && typeof target.closest === "function" &&
  (!!target.closest("input") || !!target.closest("textarea") || target.isContentEditable === true);
const keeps = (e) =>
  inField(e.target) || e.metaKey === true || e.ctrlKey === true || e.altKey === true ||
  OWN_KEYS.includes(e.key);
// A key by its place on the keyboard, which its press and its release share whatever Shift does.
const placeOf = (e) => e.code || String(e.key).toLowerCase();
const kept = new Set();
const passOn = (e, down) => send({
  type: "key", down, key: String(e.key), code: String(e.code || ""),
  shift: e.shiftKey === true, alt: e.altKey === true,
  ctrl: e.ctrlKey === true, meta: e.metaKey === true,
});
window.addEventListener("keydown", (e) => {
  if (e.repeat) return;
  if (keeps(e)) { kept.add(placeOf(e)); return; }
  kept.delete(placeOf(e));
  passOn(e, true);
}, true);
window.addEventListener("keyup", (e) => {
  if (!kept.delete(placeOf(e))) passOn(e, false);
}, true);
window.addEventListener("blur", () => kept.clear());

window.addEventListener("message", (e) => {
  if (e.origin !== origin || e.source !== window.parent) return;
  const m = e.data;
  if (!m || m.pangea !== true) return;
  try {
    if (m.type === "init") {
      // The drawing that goes: its meters move no more, and say where each stood.
      const stood = meter ? meter.stop() : null;
      meter = null;
      if (stage) stage.stop();
      stage = null;
      // A planner's arrangement is fitted to the pages of the plugin's panel with no plan, its
      // candidates counted where nobody sees them with the spec drawPanel draws (the meters
      // included). Any other arrangement is built as it is.
      const fitted = fitPanel(m.data, (p) => countPages(window.Pangea, drawnSpec(p), p.params));
      // A planned panel that still needs more than two pages is drawn as the best the fit found,
      // and said so (decision 97).
      if (fitted.fit && fitted.fit.pages > MOST_PAGES)
        console.warn("Pangea: this planned panel needs " + fitted.fit.pages + " pages after the fit (" + fitted.fit.step + ").");
      panel = fitted.panel;
      root.replaceChildren();
      // Drawn, and at once nothing the library drew moves by itself. A drawing is not a change
      // in the sound: a meter the last drawing had too stands where it stood and goes on from
      // there. Any other meter is at rest until the site sends its value.
      const drawn = drawPanel(root, panel, stood);
      ctx = drawn.ctx;
      meter = drawn.meter;
      stage = drawn.stage;
      for (const el of meter.elements) el.setAttribute("data-help-fixed", "meter");
      linkFooter(root, origin);
      heads = playheadLighter(root, panel);
      apply(m.data.values || {});
      markControls();
      markName();
      // The new drawing's header: trimmed, and its button set from the site's state.
      bypassed = panel.bypass !== null && panel.bypass.on;
      showBypass();
      setEditing(m.editing);
      choose(m.chosen == null ? null : m.chosen);
      // After a fit, the site is told what was drawn, so that Edit layout starts from it; and
      // after any drawing, what it shows (docs/panel-planner.md).
      const arranged = m.data.ui && m.data.ui.arrangement;
      const shown = drawnOf(panel, arranged, fitted.fit ? fitted.fit.pages : pagesIn(root), fitted.fit);
      // The site's tag for this drawing (the version it showed), sent back as it came, so that
      // what was drawn is kept with that version even when the site has moved on since.
      const tag = typeof m.tag === "string" ? { tag: m.tag } : {};
      send(fitted.fit ? { type: "drawn", fit: fitted.fit, panel: shown, ...tag } : { type: "drawn", panel: shown, ...tag });
      requestAnimationFrame(() => requestAnimationFrame(tell));
    } else if (m.type === "params") apply(m.values || {});
    else if (m.type === "display" && heads) heads.show(Array.isArray(m.slots) ? m.slots : null);
    else if (m.type === "edit") { setEditing(m.on); choose(m.chosen == null ? null : m.chosen); }
    else if (m.type === "bypass") { bypassed = m.on === true; showBypass(); }
    else if (m.type === "meters" && meter) meter.show(m.values);
    else if (m.type === "notes" && stage) stage.notes(m.events);
    else if (m.type === "quiet" && stage) stage.quiet();
  } catch (error) { fail(error && error.message ? error.message : error); }
});
send({ type: "ready" });
