// Its Felt parts derive from Pattern Player by Lukasz Furman: licensed as shell/platform/web/FELT-LICENSE.md says. Also offered under AGPL-3.0-or-later, at your choice (25 November 2026).
// One mapping for the plugin's web view (host.js) and the builder's panel on the site (builder.js):
// the plugin's controls and its ui.json in, the component library's parameters and spec out. Both
// pages draw with Pangea.render from what this answers, so a plugin looks the same in both.
//
// Each Faust control becomes a library parameter (a menu becomes a choice, a check box a toggle, a
// button a momentary press) and goes into the section of the Faust group that holds it. A version's
// own look (ui.look, decision 97) is drawn with every field the library has; a version without one
// is drawn in its style, one of the five looks kept as presets (PRESET_LOOKS).
//
// A logic module's controls (a MIDI tool's, docs/midi-tools.md, "Controls") come the same way, with
// their hint in `style`: a menu in Faust's menu form is a choice, "bars" a vertical fader, "generate"
// a number box, "knob" a knob. A bar with a playhead (`playhead`, the display slot) lights while its
// slot shows its place among the bars of its group that watch that slot, as both native and web
// panels count it; playheadLighter below does the lighting, since the library has no such state.
//
// A Faust plugin's steps light the same way (decision 89). Its sound code marks each step's
// control with [playhead:N], of any kind, and declares one bar graph with the same hint whose
// value is the step playing; the host reads that value as it reads a meter's and sends it as
// display slot N. A control the host sends with `playhead` watches that slot, whatever it is.
//
// An arrangement (ui.arrangement) is a person's, from Edit layout, or the panel planner's (decision
// 79), which also names a layout family and the hero. Both are read here the same way. Since
// decision 97 the planner gives each control a role (hero, main or other), never a size: the code
// chooses the size from the control's role and the room the family has (sizeForRole), and a size
// or a component a person set always wins. Since docs/panel-planner.md (change 6) the planner
// may prefer a component (`prefer`): it is drawn when the control's kind and the layout allow it
// (plannedComponent), the fit draws a compact one in its place when it costs a page, and otherwise
// the code chooses (componentFor). What was drawn, and each preference that was not, the page
// reports (drawnOf). A planner's
// arrangement is fitted first (fitPanel below): for a version with a look, smaller sizes, then
// sections of one or two controls merged, then a hero_knob or display_first panel moved to a flat
// panel (decision 97); for one without, as decision 90 fitted it. A person's arrangement is drawn
// as it is stored. Both are drawn on a page of PAGE_ROWS rows. No two controls show one label, and
// a choice with too many names for segmented buttons or a rotary is a dropdown.
//
// The Felt look (docs/felt-look.md) is Pattern Player's, from the Hand family: the material felt,
// Jost, the felt_ring knob and its palette (FELT_LOOK), which any plugin may wear, and for a MIDI tool the
// side_columns family as well: its hands in a column at each side, the XY pad, the chord map and the
// notes in the middle, and the variation as Generate along the foot (sideColumns). The chord map and the
// notes follow the notes the tool plays, which a host that can send them says it will (`notes: true`)
// and then sends as they sound; stageDriver reads them with ./harmony.js.
//
// Every plugin has one bypass and its host supplies it (decisions 74 and 75). A host that does
// sends `bypass: { on }`, and only then is the library's header bar drawn: the name on the left and
// the bypass button at the far right. hostBypass below hands that button to the host. The site's
// test bench is one such host and a plugin's editor is the other.
//
// A meter on a panel shows the plugin's own sound, and nothing on a panel moves unless it follows
// the sound or a control (decision 76). A host that reads the plugin's meters sends `meters`, the
// bar graphs its sound code declares, and buildPanel answers every one to draw (decision 88): a
// left and right pair as one stereo meter, each with the face its kind calls for, in the section
// that holds its Faust group, last in it. The library is asked for them only by drawPanel below,
// which draws the panel and hands the meters to meterDriver in one call: the library draws a
// meter a host binds at rest, and the driver moves each from the values the host sends, and from
// nothing else. A page that draws with Pangea.render itself gets no meter at all. Both pages draw
// with drawPanel: the builder's with the meters the site sends, the plugin's with the meters it
// finds in its sound code's tree (faustMeters, decision 91), whose values its editor sends.

import { harmonyNow, NotesBook, noteName, parseTonic } from "./harmony.js";

// ---------- Faust controls to library parameters and sections ----------
const menuOf = (style) => {
  const m = /^(menu|radio)\{(.*)\}$/s.exec(style || "");
  if (!m) return null;
  const items = [...m[2].matchAll(/'([^']*)'\s*:\s*(-?[\d.]+)/g)].map((x) => ({ label: x[1], value: +x[2] }));
  return items.length ? items : null;
};
// A label drops its section's name ("Comp Attack ms" under Compressor reads "Attack"), and its unit,
// unless nothing but the unit would be left ("Input dB" under Input stays "Input").
const shorten = (label, section, unit) => {
  const noUnit = (s) => (unit && s.toLowerCase().endsWith(" " + unit.toLowerCase()) ? s.slice(0, -unit.length - 1) : s);
  const words = label.split(/\s+/);
  const sec = section.toLowerCase();
  let rest = label;
  if (label.toLowerCase().startsWith(sec + " ")) rest = label.slice(section.length + 1);
  else if (words.length > 1 && words[0].length >= 3 && sec.split(/\s+/)[0].startsWith(words[0].toLowerCase())) rest = words.slice(1).join(" ");
  rest = noUnit(rest);
  if (!rest || (unit && rest.toLowerCase() === unit.toLowerCase())) return noUnit(label);
  return rest;
};

// What each kind of parameter may be drawn as, in the library's names. The first is what the look
// gives when nothing is chosen ("" stands for the look's own switch or selector). The site's editor
// offers the same table (src/lib/ui/arrangement.ts; a test holds the two together).
export const DRAWN_AS = {
  continuous: ["knob", "fader", "fader_h"],
  stepped: ["knob", "number_box"],
  choice: ["dropdown", "segmented", "stepped_rotary"],
  toggle: ["toggle", "rocker", "lit_button", "segmented"],
  momentary: ["lit_button"],
};

/**
 * The library's layout families an arrangement or a look may name: the three the preset looks use
 * (decision 79), and since decision 97 all six. The site's schema has the same list (PANEL_FAMILIES
 * in src/lib/ui/schema.ts; a test holds the two together).
 */
//
// side_columns, the seventh, is the Felt look's for a MIDI tool (docs/felt-look.md): a column at each
// side for the sections whose names say left or right, the stage and the rest in the middle. The panel
// planner is never offered it (PLANNED_FAMILIES in src/lib/ui/schema.ts).
export const PANEL_FAMILIES = ["hardware_unit", "flat_panel", "hero_knob", "display_first", "poster", "synth_page", "side_columns"];

/**
 * The fields of a look (ui.look, decision 97) that name one of the library's own options, each
 * with what the library offers. The site's schema has the same lists (src/lib/ui/schema.ts; a test
 * holds them together). A look whose layout or material is not one of these is no look, and the
 * preset is drawn; any other field that is not one of these takes the library's default.
 */
export const LOOK_CHOICES = {
  layout: PANEL_FAMILIES,
  material: ["matte", "brushed_metal", "painted_metal", "bakelite", "wood", "paper", "felt"],
  variant: ["light", "dark"],
  knob_style: ["flat_ring", "metal_cap", "bakelite_cap", "chicken_head", "hero", "felt_ring"],
  fader_style: ["flat_line", "channel_strip"],
  switch_style: ["toggle", "rocker", "lit_button", "segmented"],
  selector_style: ["dropdown", "stepped_rotary", "number_box", "tempo_sync"],
  group_style: ["none", "rule", "card", "inset", "bracket", "block"],
  tab_style: ["underline", "pill", "plate", "block"],
};
export const LOOK_FONTS = [
  "grotesk_1", "grotesk_2", "grotesk_3", "grotesk_4",
  "condensed_1", "condensed_2", "condensed_3", "condensed_4",
  "mono_1", "mono_2", "mono_3", "mono_4",
  "serif_1", "serif_2", "serif_3", "serif_4",
  "display_1", "display_2", "display_3", "display_4",
  // Jost, the Felt look's font (docs/felt-look.md), in no group: no direction draws it.
  "jost",
];
export const LOOK_DETAILS = ["screws", "wear", "grain", "seams", "glow"];
const LOOK_DEFAULTS = {
  variant: "dark", knob_style: "flat_ring", fader_style: "flat_line", switch_style: "toggle",
  selector_style: "dropdown", group_style: "rule", tab_style: "underline",
};
const HEX = /^#[0-9a-fA-F]{6}$/;
const TITLE_SIZES = ["small", "medium", "large"];
const TITLE_PLACES = ["top_left", "top_centre", "banner"];

/**
 * A version's own look (ui.look, decision 97), each field checked, or null for a version that has
 * none and draws its preset. A colour that is not #rrggbb takes the palette's own (ui.palette), so
 * a look is never drawn in colours nobody chose.
 */
export function lookOf(ui) {
  const l = ui && ui.look;
  if (!l || typeof l !== "object") return null;
  if (!LOOK_CHOICES.layout.includes(l.layout) || !LOOK_CHOICES.material.includes(l.material)) return null;
  const out = { layout: l.layout, material: l.material };
  for (const field of Object.keys(LOOK_DEFAULTS)) {
    out[field] = LOOK_CHOICES[field].includes(l[field]) ? l[field] : LOOK_DEFAULTS[field];
  }
  out.font = LOOK_FONTS.includes(l.font) ? l.font : "grotesk_1";
  out.details = Array.isArray(l.details) ? l.details.filter((d) => LOOK_DETAILS.includes(d)) : [];
  const given = l.palette && typeof l.palette === "object" ? l.palette : {};
  const four = Object.assign({ bg: "#131417", panel: "#24262c", text: "#e8e6e1", accent: "#ff6b3d" }, (ui && ui.palette) || {});
  const colour = (name, fallback) => (typeof given[name] === "string" && HEX.test(given[name]) ? given[name] : fallback);
  out.palette = {
    bg: colour("bg", four.bg),
    panel: colour("panel", four.panel),
    text: colour("text", four.text),
    accent: colour("accent", four.accent),
  };
  out.palette.highlight = colour("highlight", out.palette.accent);
  out.header_bar = l.header_bar !== false;
  const t = l.title && typeof l.title === "object" ? l.title : {};
  out.title = {
    text: typeof t.text === "string" && t.text.trim() ? t.text.trim().slice(0, 60) : "",
    size: TITLE_SIZES.includes(t.size) ? t.size : "medium",
    placement: TITLE_PLACES.includes(t.placement) ? t.placement : "top_left",
  };
  return out;
}

/**
 * The five looks a version chose by its style before decision 97, kept as presets so that every
 * plugin built before keeps its look. A version with a look of its own (lookOf) is not drawn in one.
 */
export const PRESET_LOOKS = {
  vintage: { layout: "hardware_unit", material: "painted_metal", font: "condensed_1", knob_style: "metal_cap", details: ["screws"] },
  minimal: { layout: "flat_panel", material: "matte", font: "grotesk_1", knob_style: "flat_ring", details: [] },
  retro: { layout: "hardware_unit", material: "bakelite", font: "mono_1", knob_style: "bakelite_cap", details: ["seams"] },
  playful: { layout: "flat_panel", material: "paper", font: "display_2", knob_style: "flat_ring", details: ["grain"] },
  pedal: { layout: "hero_knob", material: "painted_metal", font: "condensed_2", knob_style: "chicken_head", details: ["screws"] },
};

/**
 * The roles the planner gives controls (decision 97): the hero, the main controls, and the rest.
 * It never names a component or a size (decision 66); the code below chooses them from the
 * control's kind, its role and the room the layout has (sizeForRole, componentFor).
 */
export const CONTROL_ROLES = ["hero", "main", "other"];

/**
 * The size the library gives every control of a family by how many there are (AUTO in pangea.js,
 * the same table): a few controls are drawn large, many small.
 */
export const LIBRARY_SIZES = {
  display_first: (n) => (n <= 5 ? "m" : "s"),
  flat_panel: (n) => (n <= 6 ? "l" : n <= 14 ? "m" : "s"),
  hardware_unit: (n) => (n <= 5 ? "l" : n <= 12 ? "m" : "s"),
  hero_knob: (n) => (n <= 6 ? "m" : "s"),
  synth_page: (n) => (n <= 8 ? "m" : "s"),
  poster: (n) => (n <= 4 ? "l" : n <= 10 ? "m" : "s"),
};
const SIZE_UP = { s: "m", m: "l", l: "l" };
const SIZE_DOWN = { s: "s", m: "s", l: "m" };

/**
 * The families whose controls sit under a stage (the hero, or the display) at the library's own
 * size: no control there is given a size, and the hero is drawn in the stage.
 */
export const STAGE_FAMILIES = ["hero_knob", "display_first"];

/**
 * A control's size from its role and the room (decision 97). The room is the size the library would
 * give every control of this family and count (LIBRARY_SIZES). Main controls take that size, and
 * at least medium; the hero one size larger; every other control one size smaller, and at least
 * small. So the controls a player reaches for are larger than the rest and the rest are one size.
 */
export function sizeForRole(role, family, count) {
  const room = (LIBRARY_SIZES[family] || LIBRARY_SIZES.flat_panel)(count);
  const main = room === "s" ? "m" : room;
  if (role === "hero") return SIZE_UP[main];
  if (role === "main") return main;
  return SIZE_DOWN[room];
}

/** The most choices a segmented control and a stepped rotary may show (fault 5 of 12 November). */
export const SEGMENTED_MOST = 4;
export const ROTARY_MOST = 6;
/**
 * The longest name the code puts on a segmented button, and around a stepped rotary, when it
 * chooses one: a longer name is cut on a button, and around a rotary, which shows every name in
 * full, it widens the control (decision 97), so the code takes a dropdown.
 */
export const SEGMENTED_NAME_MOST = 8;
export const ROTARY_NAME_MOST = 10;

/**
 * A choice's component made usable: a segmented control with more than four choices, or a stepped
 * rotary with more than six, becomes a dropdown. `as` is what it would be drawn as.
 */
export const usableChoice = (as, count) =>
  (as === "segmented" && count > SEGMENTED_MOST) || (as === "stepped_rotary" && count > ROTARY_MOST)
    ? "dropdown"
    : as;

/** What the library draws a choice as when nothing names a component (pick in pangea.js). */
const libraryChoice = (count, switchStyle, selectorStyle) =>
  switchStyle === "segmented" && count <= SEGMENTED_MOST
    ? "segmented"
    : selectorStyle === "tempo_sync"
      ? "dropdown"
      : selectorStyle;

/**
 * The component the planner preferred for a control (change 6), as `{ as }` when it can be
 * drawn so, or `{ why }` when it cannot: "kind" when the control's kind cannot take it (DRAWN_AS),
 * "names" when its choices are too many or too long for segmented buttons or a rotary (the limits
 * componentFor keeps), and "layout" for a rotary under a stage (hero_knob, display_first), which
 * has no room for its names. Anything else the kind takes is drawn; the fit guards the room.
 */
export function plannedComponent(p, wanted, family) {
  if (!(DRAWN_AS[p.kind] || []).includes(wanted)) return { why: "kind" };
  if (p.kind === "choice") {
    const n = p.choices.length;
    const longest = Math.max(0, ...p.choices.map((c) => String(c).length));
    if (wanted === "segmented" && (n > SEGMENTED_MOST || longest > SEGMENTED_NAME_MOST)) return { why: "names" };
    if (wanted === "stepped_rotary") {
      if (STAGE_FAMILIES.includes(family)) return { why: "layout" };
      if (n < 2 || n > ROTARY_MOST || longest > ROTARY_NAME_MOST) return { why: "names" };
    }
  }
  return { as: wanted };
}

/** A level a player rides: the plugin's own output or volume. */
const RIDDEN = /^(output|out|volume|vol|master|level)\b/i;

/**
 * The component the code gives a control of a planned panel with a look (decision 97), from its
 * kind, its role, its family and the room (`count`, the controls the family sizes), or null to
 * leave it to the library. Two to four short choices are
 * a segmented control, more or longer ones a dropdown, and three to six a stepped rotary when the
 * look's selectors are rotaries, the names are short enough and the family has room for one. The one control a player rides as a level is a
 * fader when the look's faders are a channel strip and the family has room for medium controls.
 * Switches, momentary buttons and stepped controls take the look's own.
 */
export function componentFor(p, look, family, role, riddenTaken, count) {
  const stage = STAGE_FAMILIES.includes(family);
  if (p.kind === "choice") {
    const n = p.choices.length;
    const longest = Math.max(0, ...p.choices.map((c) => String(c).length));
    if (!stage && look.selector_style === "stepped_rotary" && n >= 3 && n <= ROTARY_MOST && longest <= ROTARY_NAME_MOST)
      return "stepped_rotary";
    return n <= SEGMENTED_MOST && longest <= SEGMENTED_NAME_MOST ? "segmented" : "dropdown";
  }
  // A fader is tall: only on a panel whose room gives its controls medium size or more.
  const roomy = (LIBRARY_SIZES[family] || LIBRARY_SIZES.flat_panel)(count || 0) !== "s";
  if (p.kind === "continuous" && role !== "hero" && !stage && !riddenTaken && roomy && look.fader_style === "channel_strip" &&
    ["hardware_unit", "synth_page"].includes(family) && RIDDEN.test(p.label)) return "fader";
  return null;
}

/**
 * No two controls show the same label (fault 2 of 12 November: "High Hz" and "High dB" were both
 * HIGH). Of each set of shown controls that would read the same, each is given the first of these
 * that no other control shows: its own label from the sound code, its section's name before its
 * label, then its label with a number. A label is at most 40 characters.
 */
export function uniqueLabels(shown, sectionOf, ownLabel) {
  const norm = (s) => String(s).trim().toLowerCase().replace(/\s+/g, " ");
  const taken = new Map();
  for (const p of shown) {
    const k = norm(p.label);
    taken.set(k, (taken.get(k) || 0) + 1);
  }
  // Every control of a set that reads the same is given its own, not all but one of them.
  const clashing = shown.filter((p) => (taken.get(norm(p.label)) || 0) > 1);
  for (const p of clashing) {
    const k = norm(p.label);
    const section = sectionOf(p.id) || "";
    const words = [ownLabel(p.id), section && !norm(p.label).startsWith(norm(section)) ? section + " " + p.label : ""];
    let next = "";
    for (const w of words) {
      const c = String(w || "").trim().slice(0, 40);
      if (c && !taken.has(norm(c))) { next = c; break; }
    }
    for (let i = 2; !next; i += 1) {
      const c = (String(p.label).slice(0, 36) + " " + i).trim();
      if (!taken.has(norm(c))) next = c;
    }
    taken.set(k, taken.get(k) - 1);
    taken.set(norm(next), 1);
    p.label = next;
  }
}

/**
 * The display of an arrangement (decision 97), bound to the shown controls by key, as the library's
 * spec takes it (by id), or null when it is missing or bound to a control that is not shown. xy_pad
 * is always null: Pangea draws no XY pad since 17 Nov 2026. A display is a filter response with one handle per band or an envelope; it
 * never shows a spectrum, since no sound reaches the panel (decision 76).
 */
export function partsOf(parts, byKey, shownIds) {
  const out = { display: null, xy_pad: null };
  if (!parts || typeof parts !== "object") return out;
  const id = (key, continuousOnly) => {
    const p = typeof key === "string" ? byKey[key] : null;
    if (!p || !shownIds.has(p.id)) return null;
    if (continuousOnly && p.kind !== "continuous") return null;
    return p.id;
  };
  const d = parts.display;
  if (d && typeof d === "object" && d.type === "filter_response" && Array.isArray(d.handles)) {
    const seen = new Set();
    const handles = [];
    for (const h of d.handles.slice(0, 8)) {
      const x = h && id(h.x, true);
      if (!x || seen.has(x)) continue;
      seen.add(x);
      const handle = { x };
      const y = id(h.y, true), q = id(h.q, true);
      if (y) handle.y = y;
      if (q) handle.q = q;
      if (["bell", "ls", "hs", "hp", "lp"].includes(h.type)) handle.type = h.type;
      handles.push(handle);
    }
    if (handles.length) {
      out.display = { type: "filter_response", spectrum: false, handles };
      if (typeof d.label === "string" && d.label.trim()) out.display.label = d.label.trim().slice(0, 20);
    }
  } else if (d && typeof d === "object" && d.type === "envelope" && d.bind && typeof d.bind === "object") {
    const bind = {};
    for (const k of ["a", "d", "s", "r"]) {
      const b = id(d.bind[k], true);
      if (b) bind[k] = b;
    }
    if (Object.keys(bind).length >= 2) {
      out.display = { type: "envelope", bind };
      if (typeof d.label === "string" && d.label.trim()) out.display.label = d.label.trim().slice(0, 20);
    }
  }
  // No XY pad, even one an older plan stored: Pangea took it out (17 Nov 2026), because a
  // pad drawn here moved on one axis only.
  return out;
}

/**
 * The rows of a page of controls, for the builder's panel and the plugin's (decision 90). The
 * library lays a panel out on its own grid, a row being 8 of its units, and makes a new page, with
 * tabs, when the sections need more rows than a page holds: 46 unless the spec says otherwise
 * (`page_rows`, a Pangea patch to the library). At 46 the Step Drive Filter, 31 controls and its
 * meters, needed three pages at 900 pixels wide, planned or not. Drawn by the library, its plan
 * with the sizes the fit makes smaller (every medium control small and every large one medium)
 * needs 79 rows: with v4's eleven meters on the builder's page, with three on the convention's
 * version, and with none on the plugin's page. So 79 is the fewest rows that give it one page,
 * and 80 is the round number above. A unit is the panel's width over 900, so at 900 pixels wide a
 * page of controls is 640 pixels high, and a panel of one page, with the header bar, the title
 * and the footer, at most about 745. A plugin's window is 720 pixels wide: there at most about
 * 600. The hero_knob family keeps the rows its hero stands in out of a page, as the library
 * always has.
 */
export const PAGE_ROWS = 80;

/** A control's key, as src/lib/ui/keys.ts makes it: the groups below the root, then the label. */
export const keyOf = (c) => [...c.groups.slice(1), c.label].join("/");

/**
 * The name of the member who built the plugin, for the footer ("Built by <name> with Pangea"): what
 * the site or the plugin sent as `creator`, on one line and at most 60 characters. Empty when they
 * sent none, and the library then writes "Pangea artist".
 */
export const creatorOf = (data) =>
  typeof data.creator === "string" ? data.creator.replace(/\s+/g, " ").trim().slice(0, 60) : "";

/**
 * The host's bypass as the page was sent it: `{ on }` when the host sent `bypass: { on: true }` or
 * `bypass: { on: false }`, and null when it sent none. The site's bench sends one, and so does a
 * plugin's editor, for the plugin's own bypass parameter (decision 75). A host that sends none
 * gets no header bar: a bypass button that does nothing is worse than no button.
 */
export const bypassOf = (data) => {
  const sent = data.bypass;
  return sent && typeof sent === "object" && typeof sent.on === "boolean" ? { on: sent.on } : null;
};

/**
 * Which of the library's meters each kind of meter is drawn as: a VU as the needle, gain
 * reduction as the library's gain reduction bar, any other level as the plain bar. Never the LED
 * ladder: at its medium and large sizes it draws a second column that is the first times 0.93,
 * which is no channel of the plugin's.
 */
export const METER_FACES = { vu: "vu_needle", gr: "gain_reduction", level: "flat_bar" };

/**
 * The meters the host sent, as far as they are well formed: each `{ id, label, groups, min, max,
 * unit, kind }`, where `id` names the meter in the values the host sends later and `kind` is
 * "vu", "gr" or "level". Empty when the host sent none. The site's bench sends a version's own.
 * The plugin's own page finds them in its sound code's tree with faustMeters below, by the site's
 * rules, and its editor sends their values (decision 91).
 */
export const metersOf = (data) => {
  const seen = new Set();
  return (Array.isArray(data.meters) ? data.meters : []).filter((m) => {
    const ok =
      !!m && typeof m === "object" && typeof m.id === "string" && m.id !== "" && !seen.has(m.id) &&
      Number.isFinite(m.min) && Number.isFinite(m.max) &&
      Object.prototype.hasOwnProperty.call(METER_FACES, m.kind);
    if (ok) seen.add(m.id);
    return ok;
  });
};

// ---------- a plugin's own meters, found in its sound code's tree (decision 91) ----------
//
// The site finds a version's meters and step displays in the compiler's tree (flattenMeters and
// flattenPlayheadSources, src/lib/faust/controls.ts) and sends the page the meters. A plugin's
// editor has no site to ask: it sends its page the tree of the sound code that runs, made the
// way the compiler makes it (PangeaMeters.h), and the page finds them here by the same rules.
// So the plugin draws the meters the builder draws for the same code. A test runs both on the
// same trees and holds them to one answer (src/lib/faust/plugin-meters.test.ts).

const TREE_GROUPS = new Set(["hgroup", "vgroup", "tgroup"]);
const TREE_BAR_GRAPHS = new Set(["hbargraph", "vbargraph"]);
const SAYS_GAIN_REDUCTION = /\bgain[\s-]*reduction\b|\bgr\b/i;

/** An item's hints ([unit:dB] and the rest) as one object: the tree lists them as pairs. */
const hintsOf = (meta) => {
  const out = {};
  for (const entry of Array.isArray(meta) ? meta : [])
    if (entry && typeof entry === "object")
      for (const [key, value] of Object.entries(entry)) out[key] = String(value);
  return out;
};

/**
 * What a bar graph shows, as the site works it out (meterKind): its hint [meter:vu], [meter:gr]
 * or [meter:level] when it has one. With none, only a bar graph in decibels can be a VU or gain
 * reduction: one whose label says gain reduction or GR is gain reduction, one on a VU's own scale
 * (from 10 to 40 below zero up to no more than 6 above) is a VU, one that starts at zero and goes
 * up is gain reduction. Anything else is a plain level.
 */
export function meterKindOf(label, hints, min, max) {
  const hint = typeof hints.meter === "string" ? hints.meter.trim().toLowerCase() : undefined;
  if (hint === "vu" || hint === "gr" || hint === "level") return hint;
  if (typeof hints.unit !== "string" || hints.unit.trim().toLowerCase() !== "db") return "level";
  if (SAYS_GAIN_REDUCTION.test(label)) return "gr";
  if (min <= -10 && min >= -40 && max > 0 && max <= 6) return "vu";
  if (min === 0 && max > 0) return "gr";
  return "level";
}

/** The display slot a [playhead:N] hint names, 0 to 7, or null, as the site reads it (playheadSlot). */
const slotOf = (hints) => {
  const raw = typeof hints.playhead === "string" ? hints.playhead.trim() : undefined;
  return raw !== undefined && /^[0-7]$/.test(raw) ? Number(raw) : null;
};

/**
 * The meters and the step displays of a plugin, from its sound code's tree. `tree` lists the
 * roots of its stages in order: a plugin's one, or an instrument's voice and then its effect
 * section, as a version stores them (decision 22). `kind` is "effect" or "instrument".
 *
 * A bar graph counts only when its value is what is heard (decision 76): never one in an
 * instrument's voice, which would show one voice of a chord, and never one at an address an
 * earlier item of the tree already has, a control or another bar graph. One with [playhead:N]
 * is a step display (decision 89), the first for each slot; every other is a meter.
 *
 * Answers `meters`, each `{ id, key, label, groups, min, max, unit, kind }`, the form the site
 * sends them in, the id being the bar graph's address, and `playheads`, each `{ id, slot }`.
 */
export function faustMeters(tree, kind) {
  const meters = [], playheads = [];
  const taken = new Set();
  const walk = (items, groups, inVoice) => {
    for (const item of Array.isArray(items) ? items : []) {
      if (!item || typeof item !== "object") continue;
      if (TREE_GROUPS.has(item.type)) {
        walk(item.items, [...groups, item.label], inVoice);
        continue;
      }
      if (!item.address) continue;
      const first = !taken.has(item.address);
      taken.add(item.address);
      if (!first || inVoice || !TREE_BAR_GRAPHS.has(item.type)) continue;
      const hints = hintsOf(item.meta);
      const slot = slotOf(hints);
      if (slot !== null) {
        if (!playheads.some((p) => p.slot === slot)) playheads.push({ id: item.address, slot });
        continue;
      }
      const min = item.min ?? 0, max = item.max ?? 1;
      meters.push({
        id: item.address,
        key: [...groups.slice(1), item.label].join("/"),
        label: item.label,
        groups,
        min,
        max,
        unit: typeof hints.unit === "string" ? hints.unit.trim() : "",
        kind: meterKindOf(item.label, hints, min, max),
      });
    }
  };
  (Array.isArray(tree) ? tree : []).forEach((root, index) => walk([root], [], kind === "instrument" && index === 0));
  return { meters, playheads };
}

/**
 * The display slots a plugin's step displays give, from the values its host sent by address:
 * slot N shows what the bar graph with [playhead:N] reports, and null while it reports nothing,
 * so every step is dark whenever the meters rest, as on the site (barGraphDisplay).
 */
export function stepSlots(playheads, values) {
  const slots = Array(8).fill(null);
  const sent = values && typeof values === "object" ? values : {};
  for (const p of Array.isArray(playheads) ? playheads : []) {
    const value = Object.prototype.hasOwnProperty.call(sent, p.id) ? sent[p.id] : null;
    slots[p.slot] = typeof value === "number" && !Number.isNaN(value) ? value : null;
  }
  return slots;
}

/** A meter's groups below the root, outermost first: its key without its label. */
const meterGroups = (m) => (Array.isArray(m.groups) ? m.groups.slice(1).map(String) : []);
/** A meter's label with its unit taken off its end ("In L dB" with unit dB is "In L"). */
const meterWords = (m) => {
  const label = typeof m.label === "string" ? m.label.trim() : "";
  const unit = typeof m.unit === "string" ? m.unit.trim() : "";
  return unit && label.toLowerCase().endsWith(" " + unit.toLowerCase()) ? label.slice(0, -unit.length - 1).trim() : label;
};
/**
 * Which side of a stereo pair a meter's label names: "In L" and "Out Left" are the left side of
 * "In" and "Out", "In R" and "Out Right" the right. The side is the last word, after a space, an
 * underscore or a hyphen, or the whole label ("L", "Right"), whose stem is then empty. Null for
 * any other label.
 */
export const sideOf = (m) => {
  const found = /^(?:(.*\S)[\s_-]+)?(l|left|r|right)$/i.exec(meterWords(m));
  if (!found) return null;
  return { stem: found[1] || "", side: found[2].toLowerCase().startsWith("l") ? "left" : "right" };
};

/**
 * The meters as they are drawn (decision 88): each meter the host sent once, in the order it was
 * sent, with a left and right pair made one stereo meter. A pair is two level meters whose labels
 * name the same stem and the two sides (sideOf), in the same group, with the same range and unit,
 * whichever of the two is sent first. Only a level is paired. Gain reduction is always one meter,
 * as the request asked, and a VU keeps its needle, which has one pointer and prints the VU's own
 * decibels. A pair takes the place of the first of its two, and its label is the stem, or its
 * group's name when the labels are the sides alone ("L" and "R"). Each answer is
 * `{ id, ids, stereo, kind, face, min, max, unit, groups, label }`: `ids` is the meter's own id,
 * or the left's and the right's, in that order, and `id` is the first of them.
 */
export function pairMeters(sent) {
  const out = [];
  const used = new Set();
  sent.forEach((m, i) => {
    if (used.has(i)) return;
    const side = m.kind === "level" ? sideOf(m) : null;
    let other = -1;
    if (side) {
      const groups = meterGroups(m).join("/");
      other = sent.findIndex((r, j) => {
        if (j <= i || used.has(j) || r.kind !== m.kind) return false;
        const theirs = sideOf(r);
        return !!theirs && theirs.side !== side.side && theirs.stem.toLowerCase() === side.stem.toLowerCase() &&
          meterGroups(r).join("/") === groups && r.min === m.min && r.max === m.max &&
          String(r.unit || "") === String(m.unit || "");
      });
    }
    used.add(i);
    if (other >= 0) used.add(other);
    const stereo = other >= 0;
    const ids = !stereo ? [m.id] : side.side === "left" ? [m.id, sent[other].id] : [sent[other].id, m.id];
    const groups = meterGroups(m);
    out.push({
      id: ids[0],
      ids,
      stereo,
      kind: m.kind,
      face: METER_FACES[m.kind],
      min: m.min,
      max: m.max,
      unit: typeof m.unit === "string" ? m.unit : "",
      groups,
      label: stereo ? side.stem || (groups.length ? groups[groups.length - 1] : "Level") : meterWords(m),
    });
  });
  return out;
}

/**
 * Which section a meter is drawn in (decision 88), as an index into `sections`, or -1 when none
 * fits. `sections` are the sections drawn, each `{ group, controls }`, and `groupsOf(id)` answers a
 * control's groups below the root. First the section that holds the most controls of the meter's
 * own group, the earliest of a tie. When no section holds any, the nearest section of the same top
 * group: the one whose controls share the most groups with the meter's, then the one that holds
 * most such controls, then the earliest. A meter at the root is in the root's section, as a control
 * there is. -1 when no section holds a control of the meter's top group either.
 */
export function meterSection(groups, sections, groupsOf) {
  const shared = (a) => {
    let n = 0;
    while (n < a.length && n < groups.length && a[n] === groups[n]) n += 1;
    return n;
  };
  let best = -1, bestDepth = -1, bestCount = 0, exact = false;
  sections.forEach((s, i) => {
    let own = 0, depth = -1, count = 0;
    for (const id of s.controls) {
      const g = groupsOf(id);
      if (!g) continue;
      if (g.length === groups.length && shared(g) === groups.length) own += 1;
      const d = shared(g);
      if (groups.length > 0 && d === 0) continue;
      if (d > depth) { depth = d; count = 1; } else if (d === depth) count += 1;
    }
    if (own > 0) {
      if (!exact || own > bestCount) { exact = true; best = i; bestCount = own; }
      return;
    }
    if (exact || depth < 0 || groups.length === 0) return;
    if (depth > bestDepth || (depth === bestDepth && count > bestCount)) { best = i; bestDepth = depth; bestCount = count; }
  });
  return best;
}

/** A section's name without a range of steps at its end: "Pattern 1-4" is "Pattern" (src/lib/ui/step-rows.ts). */
export function withoutStepRange(name) {
  return String(name).replace(/\s*\(?\s*\d+\s*(?:-|\u2013|\u2014|to)\s*\d+\s*\)?\s*$/i, "").trim();
}

/**
 * The sections with each row of steps in one of them (decision 98), as src/lib/ui/step-rows.ts
 * joins a planned arrangement before it is stored; a test holds the two together. This draws a
 * stored plan made before then whole: on 17 November 2026 the planner split every row of 8 steps
 * in two, "Pattern 1-4" and "Pattern 5-8". `sections` are `{ group, controls }` and `rows` the
 * ids of each row in step order. A row spread over two sections or more goes, in step order, to
 * the section that holds its first step, where that step was; a section left with no control
 * goes; and that section loses a range of steps at the end of its name, unless another section
 * already has the name that leaves. Sections whose rows are whole come back unchanged.
 */
export function joinStepRows(sections, rows) {
  const same = (a, b) => String(a).toLowerCase().replace(/\s+/g, "") === String(b).toLowerCase().replace(/\s+/g, "");
  let out = sections;
  for (const row of rows) {
    const inRow = new Set(row);
    const holding = out.filter((s) => s.controls.some((id) => inRow.has(id)));
    if (holding.length < 2) continue;
    const placedIds = new Set(out.flatMap((s) => s.controls).filter((id) => inRow.has(id)));
    const steps = row.filter((id) => placedIds.has(id));
    const target = out.find((s) => s.controls.includes(steps[0])) || holding[0];
    const at = target.controls.findIndex((id) => inRow.has(id));
    const joined = Object.assign({}, target, {
      controls: [
        ...target.controls.slice(0, at).filter((id) => !inRow.has(id)),
        ...steps,
        ...target.controls.slice(at).filter((id) => !inRow.has(id)),
      ],
    });
    const plain = withoutStepRange(joined.group);
    if (plain && plain !== joined.group && !out.some((s) => s !== target && same(s.group, plain))) joined.group = plain;
    out = out
      .map((s) => (s === target ? joined : Object.assign({}, s, { controls: s.controls.filter((id) => !inRow.has(id)) })))
      .filter((s) => s.controls.length > 0);
  }
  return out;
}

// ---------- the Felt look (docs/felt-look.md) ----------

/**
 * The Felt look, Pattern Player's (the Hand family's Theme.h, carried with its author's agreement):
 * the material felt, Jost, the felt_ring knob, lit buttons, cards and its palette, the ground
 * #181714, the cards a little lighter, bone text, brass for the accent and bone for the highlight.
 * A MIDI tool wears it with the side_columns family; any other plugin keeps its own family. The
 * site holds the same look (src/lib/ui/felt.ts; a test holds the two together).
 */
export const FELT_LOOK = {
  layout: "side_columns",
  material: "felt",
  variant: "dark",
  details: [],
  font: "jost",
  palette: { bg: "#181714", panel: "#1D1B18", text: "#EDE5D5", accent: "#B99A65", highlight: "#BDB5A3" },
  knob_style: "felt_ring",
  fader_style: "flat_line",
  switch_style: "lit_button",
  selector_style: "dropdown",
  group_style: "card",
  tab_style: "pill",
  header_bar: true,
};

/** The side a section's name gives it in side_columns: "left", "right", or null for the middle. */
export function handOf(group) {
  const name = String(group || "");
  if (/\bleft\b/i.test(name)) return "left";
  if (/\bright\b/i.test(name)) return "right";
  return null;
}

/**
 * Names that read well across and up the XY pad, in order of preference: Pattern Player's pad is
 * Tension across and Articulation up.
 */
export const XY_ACROSS = ["tension", "colour", "color", "weird", "drift", "swing", "tone", "bright"];
export const XY_UP = ["articulation", "length", "touch", "density", "feel", "glitch", "evolve", "legato"];

/**
 * The two controls for the XY pad, by id: those the sound code marks [xy:x] and [xy:y] (`xy` as the
 * host sends it); else, from the continuous controls in no group (or all of them, with fewer than
 * two there), one by its name for each axis, or the first two. Null with fewer than two. `controls`
 * are the host's, in order, each with `kind` its library kind.
 */
export function chooseXY(controls) {
  const knobs = controls.filter((c) => c.kind === "continuous" && c.style !== "bars");
  const hinted = (axis) => knobs.find((c) => String(c.xy || "").trim().toLowerCase() === axis);
  const free = knobs.filter((c) => (c.groups || []).length <= 1);
  const pool = free.length >= 2 ? free : knobs;
  const byName = (names, not) => {
    for (const name of names) {
      const found = pool.find((c) => c !== not && String(c.label).toLowerCase().includes(name));
      if (found) return found;
    }
    return undefined;
  };
  const x = hinted("x") || byName(XY_ACROSS) || pool[0];
  const y = hinted("y") || byName(XY_UP, x) || pool.find((c) => c !== x);
  return x && y && x !== y ? { x: x.id, y: y.id } : null;
}

/** A menu whose name says what the chord map reads or sets, by Faust label. */
const KEY_MENUS = { key: /^(key|root|tonic)$/i, mode: /^(mode|scale)$/i, chord: /^chord$/i };

/**
 * A MIDI tool's sections in side_columns (the Felt lab's rules, src/components/felt/layout.ts until
 * 19 November 2026): a section whose name says left or right goes to that side; the controls in no
 * group go in the middle, their menus and switches as Setup, their bars as Steps and their knobs
 * along a row of their own; the XY pad takes its two controls (chooseXY) and the variation goes to
 * the foot as Generate, so neither is drawn twice. A bar is drawn as a step bar and labelled with
 * its place in its row, as Pattern Player numbers a hand's steps. `shown` are the sections as
 * placed; `byId` the library's parameters; `controls` the host's; `arrange` sets a control's entry.
 */
export function sideColumns(shown, byId, controls, arrange) {
  const host = {};
  for (const c of controls) host[c.id] = c;
  const ids = shown.flatMap((s) => s.controls).filter((id) => byId[id] && host[id]);
  const generate = ids.find((id) => host[id].style === "generate") || null;
  const xy = chooseXY(ids.map((id) => Object.assign({}, host[id], { kind: byId[id].kind })));
  const taken = new Set([generate, xy && xy.x, xy && xy.y].filter((id) => !!id));
  const setup = { group: "Setup", controls: [] }, steps = { group: "Steps", controls: [] }, strip = { group: "", controls: [] };
  const grouped = [];
  for (const s of shown) {
    const own = { group: s.group, controls: [] };
    const side = handOf(s.group);
    if (side) own.side = side;
    for (const id of s.controls) {
      if (taken.has(id) || !byId[id] || !host[id]) continue;
      const inGroup = (host[id].groups || []).length > 1;
      if (inGroup) own.controls.push(id);
      else if (host[id].style === "bars") steps.controls.push(id);
      else if (byId[id].kind === "continuous" || byId[id].kind === "stepped") strip.controls.push(id);
      else setup.controls.push(id);
    }
    if (own.controls.length) grouped.push(own);
  }
  const sections = [setup, ...grouped, steps, strip].filter((s) => s.controls.length > 0);
  // Each bar a step bar, numbered by its place in its row; four to a side's row.
  const numbered = new Set();
  for (const s of sections) {
    let place = 0;
    for (const id of s.controls) {
      if (host[id].style !== "bars") continue;
      place += 1;
      arrange(id, { as: "bar", size: "m" });
      byId[id].label = String(place);
      // Named for a screen reader by its hand and its place: "Left hand, step 1".
      byId[id].aria = `${s.group ? `${s.group}, ` : ""}step ${place}`;
      numbered.add(id);
    }
  }
  if (generate) arrange(generate, { as: "generate" });
  // The menus the chord map reads: the key, the mode, and a chord it may set.
  const menus = {};
  for (const id of ids) {
    if (byId[id].kind !== "choice") continue;
    for (const [what, name] of Object.entries(KEY_MENUS))
      if (!menus[what] && name.test(String(host[id].label).trim())) menus[what] = id;
  }
  const chord = menus.chord && byId[menus.chord].choices.some((c) => parseTonic(c) !== null) ? menus.chord : null;
  return { sections, xy, generate, numbered, menus, chord };
}

export function buildPanel(data) {
const params = [], sections = [], byId = {}, arrangement = {}, playheads = {}, heads = {}, stepRowOf = {};
// The planner's preferred components (change 6), and why any was not drawn, by id.
const prefers = {}, refused = {};
const arrange = (id, more) => { arrangement[id] = Object.assign(arrangement[id] || {}, more); };
for (const c of data.controls) {
  const section = c.groups.length > 1 ? c.groups[c.groups.length - 1] : "Main";
  const style = c.style || "";
  const p = { id: c.id, label: shorten(c.label, section, c.unit || ""), unit: c.unit || "", min: c.min, max: c.max, default: c.init };
  const menu = menuOf(style);
  if (menu) {
    menu.sort((a, b) => a.value - b.value);
    Object.assign(p, { kind: "choice", choices: menu.map((m) => m.label), values: menu.map((m) => m.value) });
    p.default = Math.max(0, p.values.indexOf(c.init));
  } else if (c.kind === "checkBox") Object.assign(p, { kind: "toggle", min: 0, max: 1 });
  else if (c.kind === "button") Object.assign(p, { kind: "momentary", min: 0, max: 1 });
  else if (style === "generate") {
    // A variation number: whole numbers, typed or dragged, with no unit to show.
    p.kind = "stepped";
    arrange(c.id, { as: "number_box" });
  } else if (c.step >= 1 && c.max - c.min <= 24) p.kind = "stepped";
  else {
    p.kind = "continuous";
    if (c.logScale && c.min > 0) p.scale = "log";
    if (c.min < 0 && Math.abs(c.min + c.max) < 1e-6) p.bipolar = true;
  }
  // A level bar: a vertical fader.
  if (style === "bars") arrange(c.id, { as: "fader_v" });
  // Its playhead, if it has one, is counted per group and slot: a MIDI tool's bar, or any
  // control of a Faust plugin that marks a step (decision 89). The host sends `playhead` only
  // for a control that watches a slot.
  const slot = Number(c.playhead);
  if (c.playhead !== undefined && c.playhead !== null && Number.isInteger(slot) && slot >= 0 && slot < 8) {
    const key = c.groups.slice(1).join("/") + "|" + slot;
    const position = heads[key] || 0;
    heads[key] = position + 1;
    playheads[c.id] = { slot, position };
    stepRowOf[c.id] = key;
  }
  // [size:s] and [size:l] mark the secondary and the main controls. The site sends them; the plugin does not yet.
  // Added to what the control is drawn as (a bar's fader, a generate's number box), not in its place.
  if (c.size === "s" || c.size === "l") arrange(c.id, { size: c.size });
  p.key = keyOf(c);
  params.push(p);
  byId[c.id] = p;
  let s = sections.find((x) => x.group === section);
  if (!s) sections.push((s = { group: section, controls: [] }));
  s.controls.push(c.id);
}

// ---------- the person's arrangement (ui.arrangement), over what the code gives ----------
const arranged = (data.ui && data.ui.arrangement) || null;
let placed = sections;
// The planner's role for each control (decision 97), by id.
const roles = {};
const byKey = {};
for (const p of params) byKey[p.key] = p;
if (arranged && typeof arranged === "object") {
  const hidden = new Set();
  const entries = arranged.controls && typeof arranged.controls === "object" ? arranged.controls : {};
  for (const key of Object.keys(entries)) {
    const p = byKey[key], e = entries[key];
    if (!p || !e || typeof e !== "object") continue;
    const a = arrangement[p.id] || {};
    // Medium means medium (decision 79), as Edit layout offers it beside "As written". Until then a
    // medium let the library choose a size by the number of controls, which drew it large on a small
    // panel and small on a large one.
    if (e.size === "s" || e.size === "m" || e.size === "l") a.size = e.size;
    if (typeof e.as === "string" && (DRAWN_AS[p.kind] || []).includes(e.as)) a.as = e.as;
    if (Object.keys(a).length) arrangement[p.id] = a; else delete arrangement[p.id];
    if (typeof e.label === "string" && e.label.trim()) p.label = e.label.trim().slice(0, 40);
    if (e.hidden === true) hidden.add(p.id);
    if (CONTROL_ROLES.includes(e.role)) roles[p.id] = e.role;
    if (typeof e.prefer === "string") prefers[p.id] = e.prefer;
  }
  if (Array.isArray(arranged.sections)) {
    // The person's sections in their order; a control they do not name keeps the section its code gives it.
    const used = new Set();
    placed = [];
    for (const sec of arranged.sections) {
      if (!sec || typeof sec.name !== "string" || !Array.isArray(sec.controls)) continue;
      const ids = [];
      for (const key of sec.controls) {
        const p = byKey[key];
        if (p && !used.has(p.id)) { used.add(p.id); ids.push(p.id); }
      }
      placed.push({ group: sec.name, controls: ids });
    }
    for (const s of sections) {
      for (const id of s.controls) {
        if (used.has(id)) continue;
        let into = placed.find((x) => x.group === s.group);
        if (!into) placed.push((into = { group: s.group, controls: [] }));
        into.controls.push(id);
      }
    }
  }
  placed = placed
    .map((s) => ({ group: s.group, controls: s.controls.filter((id) => !hidden.has(id)) }))
    .filter((s) => s.controls.length > 0);
  // A row of steps a planned arrangement split is drawn whole (decision 98). A person's own
  // arrangement is drawn as they made it.
  if (arranged.madeBy === "planner") {
    const rows = {};
    for (const id of Object.keys(stepRowOf)) (rows[stepRowOf[id]] = rows[stepRowOf[id]] || []).push(id);
    const ordered = Object.values(rows)
      .map((ids) => ids.slice().sort((a, b) => playheads[a].position - playheads[b].position))
      .filter((ids) => ids.length > 1);
    placed = joinStepRows(placed, ordered);
  }
}

// ---------- the look: the version's own (decision 97), or one of the five presets ----------
// A version with a look (ui.look) is drawn in it, every field the library has. A version without
// one, every plugin built before decision 97, is drawn in its preset as it always was.
const ui = data.ui || {};
const own = lookOf(ui);
const look = own
  ? {
      layout: own.layout, material: own.material, variant: own.variant, font: own.font, details: own.details,
      knob_style: own.knob_style, fader_style: own.fader_style, switch_style: own.switch_style,
      selector_style: own.selector_style, group_style: own.group_style, tab_style: own.tab_style,
    }
  : PRESET_LOOKS[ui.style] || PRESET_LOOKS.vintage;
const pal = own
  ? Object.assign({}, own.palette)
  : Object.assign({ bg: "#131417", panel: "#24262c", text: "#e8e6e1", accent: "#ff6b3d" }, ui.palette || {});
pal.highlight = pal.highlight || pal.accent;
const lum = (hex) => {
  const v = hex.replace("#", "");
  const n = [0, 2, 4].map((i) => parseInt(v.length === 3 ? v[i / 2] + v[i / 2] : v.substr(i, 2), 16) / 255);
  return 0.2126 * n[0] + 0.7152 * n[1] + 0.0722 * n[2];
};
const bypass = bypassOf(data);
// The Felt look's side columns for a MIDI tool (docs/felt-look.md): its sections, its XY pad,
// its variation and its chord map, before the meters are placed in the sections.
const familyNamed = arranged && typeof arranged === "object" && PANEL_FAMILIES.includes(arranged.layout) ? arranged.layout : look.layout;
const sides = familyNamed === "side_columns" ? sideColumns(placed, byId, data.controls, arrange) : null;
if (sides) placed = sides.sections;
// The plugin's own meters (decision 88): every one the host sent, a left and right pair as one
// stereo meter, each in the section that holds its Faust group (meterSection), last in it. A
// meter whose group no section holds, nor its top group, gets a section of its group's name at
// the end. Its label loses its unit and its section's name as a control's does. None is in the
// spec: drawPanel puts them there (drawnSpec), so a page that draws with Pangea.render itself
// gets no meter.
const controlGroups = {};
for (const c of data.controls) controlGroups[c.id] = c.groups.slice(1).map(String);
const meters = pairMeters(metersOf(data)).map((m) => {
  const own = m.groups.length ? m.groups[m.groups.length - 1] : "Main";
  const section = meterSection(m.groups, placed, (id) => controlGroups[id] || null);
  return {
    id: m.id,
    ids: m.ids,
    stereo: m.stereo,
    kind: m.kind,
    face: m.face,
    min: m.min,
    max: m.max,
    // A meter beside its controls is drawn small: about as tall as a large knob.
    size: "s",
    label: shorten(m.label, own, m.unit) || "Meter",
    section: section >= 0 ? placed[section].group : own,
    at: section,
  };
});
// The sections with each meter's id last in its own, as drawPanel draws them.
const metered = placed.map((s) => ({ group: s.group, controls: s.controls.slice() }));
for (const m of meters) {
  let into = m.at >= 0 ? metered[m.at] : metered.slice(placed.length).find((s) => s.group === m.section);
  if (!into) metered.push((into = { group: m.section, controls: [] }));
  into.controls.push(m.id);
}
// The layout family and the hero (decisions 79 and 97). An arrangement may name a family in place
// of the look's own; the look keeps its materials, fonts and knobs. The hero is the control the
// arrangement names as its hero, or the one whose role is the hero. The hero_knob family draws one
// continuous control oversized at the top: the hero when it is shown and continuous, otherwise the
// first continuous control shown. With no continuous control at all there is nothing to be a hero,
// and the panel is a flat panel. (The library itself would take the first control of the first
// section, a switch or a menu as well.) The display_first family draws the hero, when it is
// continuous, as a fader beside the display; any other family of a version with a look draws it one
// size larger than the rest.
const named = arranged && typeof arranged === "object" ? arranged : {};
let layout = PANEL_FAMILIES.includes(named.layout) ? named.layout : look.layout;
const shown = placed.flatMap((s) => s.controls).map((id) => byId[id]).filter((p) => !!p);
const continuous = (p) => !!p && p.kind === "continuous";
const askedHero =
  typeof named.hero === "string"
    ? shown.filter((p) => p.key === named.hero).pop() || null
    : shown.find((p) => roles[p.id] === "hero") || null;
// The display (decision 97), bound to shown controls; a display_first family with no
// display to show is a flat panel, so the library never draws a display of nothing.
const parts = partsOf(named.parts, byKey, new Set(shown.map((p) => p.id)));
if (layout === "display_first" && !parts.display) layout = "flat_panel";
let hero = null;
if (layout === "hero_knob") {
  const chosen = continuous(askedHero) ? askedHero : shown.find(continuous);
  if (chosen) hero = chosen.id;
  else layout = "flat_panel";
} else if (layout === "display_first") {
  if (continuous(askedHero)) hero = askedHero.id;
} else if (own && askedHero) hero = askedHero.id;
// Each control's component and size from its kind, its role and the room (decision 97): for a
// control with a role and no size or component of its own, which a person's or an older plan's
// arrangement sets and which always wins. In a family with a stage no control below it is sized.
const sized = shown.filter((p) => !(hero && layout === "hero_knob" && p.id === hero)).length;
// A component the planner preferred is drawn when the control's kind and the layout allow it
// (change 6); the hero of a stage is drawn in the stage, whatever was preferred.
let ridden = false;
for (const p of shown) {
  const role = roles[p.id];
  const wanted = prefers[p.id];
  if (!role && !wanted) continue;
  const a = arrangement[p.id] || {};
  if (role && !a.size && !STAGE_FAMILIES.includes(layout)) a.size = sizeForRole(role, layout, sized);
  if (!a.as) {
    const staged = hero === p.id && STAGE_FAMILIES.includes(layout);
    const chosen = wanted && !staged ? plannedComponent(p, wanted, layout) : null;
    if (chosen && chosen.as) a.as = chosen.as;
    else {
      if (chosen) refused[p.id] = chosen.why;
      const as = own && role ? componentFor(p, own, layout, role, ridden, sized) : null;
      if (as) a.as = as;
      if (as === "fader") ridden = true;
    }
  }
  if (Object.keys(a).length) arrangement[p.id] = a;
}
// A choice is never drawn as a segmented control with more than four choices or a rotary with more
// than six (fault 5): whatever would draw it so, the arrangement or the look, it is a dropdown.
const switchStyle = own ? own.switch_style : "toggle";
const selectorStyle = own ? own.selector_style : "dropdown";
for (const p of params) {
  if (p.kind !== "choice") continue;
  const a = arrangement[p.id] || {};
  const asked = a.as || libraryChoice(p.choices.length, switchStyle, selectorStyle);
  // A number box or a tempo selector shows no names: a choice is a dropdown there too.
  const drawn = DRAWN_AS.choice.includes(asked) ? asked : "dropdown";
  const usable = usableChoice(drawn, p.choices.length);
  if (usable !== asked) arrangement[p.id] = Object.assign(a, { as: usable });
}
// No two controls show the same label (fault 2).
const sectionOfId = {};
for (const s of placed) for (const id of s.controls) sectionOfId[id] = s.group;
const faustLabel = {};
for (const c of data.controls) faustLabel[c.id] = c.label;
// A step bar's number under its hand's name reads as Pattern Player's does, and is found by its
// section and its place, so it is no second label of one name (sideColumns).
uniqueLabels(sides ? shown.filter((p) => !sides.numbered.has(p.id)) : shown, (id) => sectionOfId[id], (id) => faustLabel[id]);
// The header bar holds the bypass button, so it is drawn only for a host that supplies a bypass.
const headerBar = bypass !== null;
// The title: the look's name, or the version's, as a label, the library's small title, which with
// the header bar sits at the bar's left in the look's title font. An owner chose on 17 November
// to keep the plugin title the size it was before, so a look's own title size is not drawn. With no header bar a small title is too small to read on the panel (fault of
// 17 November), so it is medium.
const titleText = (own && own.title.text) || ui.title || data.name;
const title = { text: titleText, size: "small", placement: own ? own.title.placement : "top_left" };
if (!headerBar && title.size === "small") title.size = "medium";
const spec = Object.assign({}, look, {
  layout,
  ...(hero ? { hero: { control: hero } } : {}),
  variant: own ? own.variant : lum(pal.panel) < 0.5 ? "dark" : "light",
  palette: pal,
  switch_style: switchStyle,
  selector_style: selectorStyle,
  header_bar: headerBar,
  sections: placed,
  arrangement,
  // A taller page than the library's own, so that a panel needs one page where it can (PAGE_ROWS).
  page_rows: PAGE_ROWS,
  title,
  // Who built it: the project's owner, by name. The footer says so, and links to Pangea (linkFooter).
  creator: creatorOf(data),
  ...(parts.display ? { display: parts.display } : {}),
});
// Never an XY pad, even one a stored look carries (taken out on 17 Nov 2026), except the one
// side_columns binds itself to two real controls (docs/felt-look.md).
delete spec.xy_pad;
let harmony = null;
if (sides && layout === "side_columns") {
  if (sides.xy) spec.xy_pad = sides.xy;
  if (sides.generate) spec.generate = sides.generate;
  // The chord map and the notes, only for a host that sends the notes the tool plays.
  if (data.notes === true) {
    spec.chord_map = { label: "Chord map", choose: sides.chord };
    spec.note_roll = { label: "Notes", note: "the last 8 seconds" };
    harmony = { menus: sides.menus, chord: sides.chord };
  }
}
return { params, sections: placed, byId, spec, playheads, bypass, meters, metered: meters.length ? metered : placed, prefers, refused, harmony };
}

/**
 * The footer ("Built by <name> with Pangea") as a link to Pangea, so that anyone shown a plugin can
 * find where it was made. The library draws the line as plain text and draws it once for each
 * Pangea.render, so the page puts what it drew inside a link after each drawing. `site` is Pangea's
 * address. `open`, when given, is called in place of following the link: the plugin's web view has
 * no tabs, so its editor hands the address to the system's browser. Without an address that is http
 * or https nothing is linked. Answers the link, or null. To go once the library draws the link itself.
 */
export function linkFooter(root, site, open) {
  const foot = root.querySelector(".pg-foot");
  if (!foot || foot.querySelector("a")) return null;
  if (typeof site !== "string" || !/^https?:\/\/[^\s/]/i.test(site)) return null;
  if (!document.getElementById("pangea-foot-link")) {
    const style = document.createElement("style");
    style.id = "pangea-foot-link";
    // The link is the footer's one child and lays the words out as the footer did, so a long name
    // still shortens with an ellipsis. It is as tall as the footer, to be easy to press, and keeps
    // the footer's ink: only its underline says link.
    style.textContent =
      ".pg-foot a{display:flex;align-items:center;align-self:stretch;min-width:0;color:inherit;text-decoration:none;" +
      "cursor:pointer;border-radius:calc(3*var(--u))}" +
      ".pg-foot a:hover,.pg-foot a:focus-visible{text-decoration:underline;text-underline-offset:calc(3*var(--u))}" +
      ".pg-foot a:focus-visible{outline:calc(2*var(--u)) solid var(--highlight,var(--accent));outline-offset:calc(3*var(--u))}";
    document.head.appendChild(style);
  }
  const link = document.createElement("a");
  link.href = site;
  link.target = "_blank";
  link.rel = "noopener";
  link.append(...foot.childNodes);
  foot.append(link);
  if (typeof open === "function") {
    link.addEventListener("click", (e) => {
      e.preventDefault();
      open();
    });
  }
  return link;
}

/** What each bypass button tells of a press: the `ask` of the latest call for that button. */
const asks = new WeakMap();

/**
 * The header bar's bypass button, handed to the host (decision 74). The library draws the bar with
 * a preset stepper of made-up names, A and B buttons and a bypass button that only fades the panel
 * by itself, and draws it afresh for each Pangea.render. So after each drawing the page takes the
 * stepper and A and B out, since they do nothing, and takes the button over. A press never runs the
 * library's own toggle: it calls `ask` with what was asked for, true to bypass and false to bring
 * the plugin back. The button, the word "Bypassed" beside it and the panel's fade change only here,
 * from `on`, the state the host gave: the host's state is the truth. Call it again when the host's
 * state changes; a drawing it has already trimmed is not trimmed again. Answers the button, or null
 * when the drawing has no header bar, and no fade is left on then. To go once the library has a
 * bypass that a host can drive.
 */
export function hostBypass(root, on, ask) {
  const bar = root.querySelector(".pg-hdr");
  const button = bar ? bar.querySelector(".pg-byp") : null;
  const bypassed = !!button && on === true;
  // The library's stylesheet fades everything but the bar while the root has this class. The root
  // outlives a drawing, so the class is settled here every time, header bar or none.
  root.classList.toggle("is-bypassed", bypassed);
  if (!button) return null;
  if (!document.getElementById("pangea-bypass")) {
    const style = document.createElement("style");
    style.id = "pangea-bypass";
    // The word takes the bar's own type and never wraps or shrinks: the name beside it shortens
    // first. The library strips a button of its focus ring, so the keyboard's focus is drawn here.
    style.textContent =
      ".pg-byp-word{flex:none;white-space:nowrap}" +
      ".pg-byp-word[hidden]{display:none}" +
      ".pg-hdr .pg-byp:focus-visible{outline:calc(2*var(--u)) solid var(--highlight,var(--accent));" +
      "outline-offset:calc(3*var(--u))}";
    document.head.appendChild(style);
  }
  for (const part of [".pg-preset", ".pg-ab"]) {
    const el = bar.querySelector(part);
    if (el) el.remove();
  }
  let word = bar.querySelector(".pg-byp-word");
  if (!word) {
    word = document.createElement("span");
    word.className = "pg-byp-word";
    word.textContent = "Bypassed";
    bar.insertBefore(word, button);
  }
  word.hidden = !bypassed;
  // As the library draws it, the button is lit (is-on) while the plugin is in and dark while it is
  // bypassed. To a screen reader it is a toggle named Bypass, pressed while bypassed.
  button.classList.toggle("is-on", !bypassed);
  button.setAttribute("aria-label", "Bypass");
  button.setAttribute("aria-pressed", bypassed ? "true" : "false");
  if (!asks.has(button)) {
    // On the button itself, so it runs before the press reaches the bar, where the library listens.
    button.addEventListener("click", (e) => {
      e.stopPropagation();
      const tell = asks.get(button);
      if (typeof tell === "function") tell(button.getAttribute("aria-pressed") !== "true");
    });
  }
  asks.set(button, ask);
  return button;
}

/**
 * Lights the controls whose playhead slot shows their place: a MIDI tool's bar, or a Faust
 * plugin's step (decision 89), while its step plays. The library keeps no such state, so the
 * page finds each control's element by its section and label, once, and again whenever the
 * library has drawn it afresh. `show` takes the eight display slots, a negative number or null
 * where a slot shows nothing.
 */
export function playheadLighter(root, panel) {
  if (!document.getElementById("pangea-playhead")) {
    const style = document.createElement("style");
    style.id = "pangea-playhead";
    style.textContent =
      ".pg-ctl.pg-playhead{outline:calc(2*var(--u)) solid var(--highlight,var(--accent));" +
      "outline-offset:calc(3*var(--u));border-radius:calc(6*var(--u))}";
    document.head.appendChild(style);
  }
  const ids = Object.keys(panel.playheads || {});
  let found = null;
  const labelOf = (el) => {
    const lbl = el.querySelector(".pg-lbl");
    const span = lbl && lbl.querySelector("span");
    return ((span || lbl) ? (span || lbl).textContent : "").trim();
  };
  const find = () => {
    found = {};
    const bySection = {}, byLabel = {};
    for (const s of panel.sections)
      for (const id of s.controls) {
        const label = panel.byId[id].label;
        bySection[s.group + "\u0000" + label] = id;
        (byLabel[label] = byLabel[label] || []).push(id);
      }
    root.querySelectorAll(".pg-ctl").forEach((el) => {
      const label = labelOf(el);
      const group = el.closest(".pg-group");
      const title = group && group.querySelector(".pg-g-t") ? group.querySelector(".pg-g-t").textContent.trim() : null;
      let id = title !== null ? bySection[title + "\u0000" + label] : undefined;
      if (!id && byLabel[label] && byLabel[label].length === 1) id = byLabel[label][0];
      if (id && panel.playheads[id]) found[id] = el;
    });
  };
  return {
    /** How many bars have a playhead, and how many of them the page found. */
    count() {
      if (!found) find();
      return { bars: ids.length, found: Object.keys(found).length };
    },
    show(slots) {
      if (!ids.length) return;
      if (!found || ids.some((id) => found[id] && !found[id].isConnected)) find();
      for (const id of ids) {
        const head = panel.playheads[id];
        const v = slots ? slots[head.slot] : null;
        const lit = v !== null && v !== undefined && v >= 0 && Math.round(v) === head.position;
        if (found[id]) found[id].classList.toggle("pg-playhead", lit);
      }
    },
  };
}

// ---------- the plugin's own meter (decision 76) ----------

/**
 * The marks on the library's VU dial, each as decibels and where along the needle's sweep it is
 * drawn, from 0 at the far left to 1 at the far right. They are not evenly spaced in decibels,
 * so a value is placed between its two neighbours. A test reads the library's file and holds
 * this table to the marks it draws: a library with another dial fails the test, and does not
 * quietly point at the wrong number.
 */
export const VU_MARKS = [[-20, 0], [-10, 0.2], [-7, 0.32], [-5, 0.42], [-3, 0.54], [0, 0.72], [3, 1]];
/**
 * The gain reduction, in decibels, at which the library's gain reduction bar is full: its scale
 * prints 0, 3, 6, 12 and 20 at even steps of the bar. Held to the library's file by the same test.
 */
export const GR_FULL_DB = 20;
/**
 * How far the plain bar's peak line falls each time the bar is set: the library's own step, a
 * part of the bar's whole height. Held to the library's file by the same test.
 */
export const BAR_PEAK_FALL = 0.004;

const clamp01 = (x) => Math.min(1, Math.max(0, x));

/**
 * A meter's value, in the meter's own unit, as the 0 to 1 its face is drawn from. `meter` is
 * buildPanel's. The needle goes through the dial's own marks, with a straight line between
 * neighbours, and stops at the first and the last. Gain reduction is decibels over the bar's 20,
 * whichever sign the code reports it with. The plain bar is the value's place in the meter's own
 * range. What is not a number is 0, where a meter rests.
 */
export function meterLevel(meter, value) {
  if (typeof value !== "number" || Number.isNaN(value)) return 0;
  if (meter.face === "vu_needle") {
    for (let i = 0; i < VU_MARKS.length; i += 1) {
      const [db, at] = VU_MARKS[i];
      if (value > db) continue;
      if (i === 0) return at;
      const [below, atBelow] = VU_MARKS[i - 1];
      return atBelow + ((value - below) / (db - below)) * (at - atBelow);
    }
    return VU_MARKS[VU_MARKS.length - 1][1];
  }
  if (meter.face === "gain_reduction") return clamp01(Math.abs(value) / GR_FULL_DB);
  return meter.max > meter.min ? clamp01((value - meter.min) / (meter.max - meter.min)) : 0;
}

/**
 * The driver moves a meter in steps of this many milliseconds, a sixtieth of a second, whatever
 * the screen's own rate. A step is one setting of the library's meter, so the plain bar's peak
 * line, which falls by a fixed part at each setting, falls at one speed on every screen: its
 * whole height in a little over four seconds.
 */
export const METER_STEP_MS = 1000 / 60;
/**
 * How each face follows a value, so that a raw value reads as that kind of meter does: the time
 * in milliseconds in which it covers all but a third or so (1/e) of the way to a higher level
 * (rise) and to a lower one (fall), and 0 for at once.
 *
 * - The needle: 65 both ways, which takes it 99 percent of the way in 300 ms. That is the
 *   movement a VU meter is built to, so a level the code did not smooth still reads as a VU.
 * - Gain reduction: 15 up and 150 down. The bar shows a reduction as it bites and lets go
 *   slowly enough for a short one to be seen.
 * - The plain bar: up at once and 250 down, as a peak meter moves, so a short peak is not missed.
 *
 * A meter falls to rest the same way it falls to any lower level.
 */
export const METER_BALLISTICS = {
  vu_needle: { rise: 65, fall: 65 },
  gain_reduction: { rise: 15, fall: 150 },
  flat_bar: { rise: 0, fall: 250 },
};
/** Nearer its value than this part of its travel, a meter is put on the value and stops. */
export const METER_SETTLED = 0.0005;
/** The most steps taken in one animation frame, after a wait such as a hidden tab's: a second. */
const METER_MAX_STEPS = 60;

/**
 * One step of a meter's movement: where a face that is drawn at `shown` is drawn one step later
 * (METER_STEP_MS), on its way to `target`. Both are the face's 0 to 1.
 */
export function meterStep(face, shown, target) {
  const times = METER_BALLISTICS[face] || METER_BALLISTICS.flat_bar;
  const time = target > shown ? times.rise : times.fall;
  const next = time > 0 ? shown + (target - shown) * (1 - Math.exp(-METER_STEP_MS / time)) : target;
  return Math.abs(target - next) < METER_SETTLED ? target : next;
}

/** In place of the library's own animation of a part: nothing. */
const still = () => {};
/** A place on a face that the last drawing's driver answered: 0 to 1, and 0 for anything else. */
const stoodAt = (x) => (typeof x === "number" && x >= 0 && x <= 1 ? x : 0);
/** Each column's place from what the last drawing's driver answered: `n` of them, each 0 to 1. */
const stoodEach = (x, n) => Array.from({ length: n }, (_, k) => stoodAt(Array.isArray(x) ? x[k] : undefined));

/**
 * The plugin's own meters, driven from outside the library (decisions 76 and 88). The library
 * draws a meter for each that the spec lists (drawnSpec), bound to its host: it starts at rest,
 * at the left end of its dial or as an empty bar with its peak line down, and it runs no demo
 * animation. The library's animation of anything else it draws is switched off here all the
 * same, for the whole page, and taken away from each part it animates, so that a frame the
 * library had already asked for moves nothing. From then on each meter moves only when `show` is
 * given its value, a step at a time (meterStep), and stops as soon as it is where the value puts
 * it. With no value it falls to rest and stops there. A stereo meter has two columns, the left's
 * value and the right's, each moving on its own.
 *
 * A drawing is not a change in the sound. A change to the layout draws the panel again while
 * the plugin plays on, and a meter that began each drawing at rest would drop and climb back by
 * itself. So `from`, when given, is what the last drawing's driver answered when it was stopped:
 * where each meter stood, by id. A meter of this drawing that is the same one (the same id, the
 * same face, and stereo or not alike) begins where that one stood, the bar's peak lines too, and
 * goes on towards the values the host last sent. Any other meter, and every meter of the first
 * drawing, begins at rest.
 *
 * `panel` is buildPanel's, and `ctx` is what Pangea.render answered for the drawing: its
 * `meterOf(id)` answers each meter's element. Call `stop` when the drawing goes, before the next.
 * Answers `show` (the host's values by meter id: a number moves a meter's column, anything else
 * rests it), `stop`, and `elements`: the meters drawn, in the order buildPanel answered them.
 * `stop` moves the meters no more and answers where each stood, for the next drawing's `from`, or
 * null when the drawing has none. To go once the library has meters that a host can drive.
 */
export function meterDriver(root, panel, from, ctx) {
  const library = typeof window !== "undefined" ? window.Pangea : null;
  if (library && typeof library.setAnimate === "function") library.setAnimate(false);
  for (const part of [".pg-meter", ".pg-disp", ".pg-steps"])
    for (const el of root.querySelectorAll(part)) if (typeof el.frame === "function") el.frame = still;

  const find = ctx && typeof ctx.meterOf === "function" ? (id) => ctx.meterOf(id) : () => null;
  const before = from && typeof from === "object" ? from : {};
  const driven = [];
  for (const meter of Array.isArray(panel.meters) ? panel.meters : []) {
    const el = find(meter.id);
    if (!el || typeof el.setLevel !== "function") continue;
    const n = meter.stereo ? 2 : 1;
    const d = {
      meter, el, n, bar: meter.face === "flat_bar",
      shown: Array(n).fill(0), target: Array(n).fill(0), peak: Array(n).fill(0),
    };
    const was = Object.prototype.hasOwnProperty.call(before, meter.id) ? before[meter.id] : null;
    if (was && typeof was === "object" && was.face === meter.face && was.stereo === meter.stereo) {
      // Where that one stood. The peak lines are put back first, and the bar's own setting then
      // leaves them a step lower, as one more frame would.
      if (d.bar) setMeter(d, stoodEach(was.peak, n));
      d.shown = stoodEach(was.shown, n);
      d.target = stoodEach(was.target, n);
      setMeter(d, d.shown);
    }
    driven.push(d);
  }
  let last = null, waiting = 0, live = true;
  const moving = (d) => d.shown.some((s, k) => s !== d.target[k] || (d.bar && d.peak[k] > s));
  const wake = () => {
    if (!live || waiting) return;
    if (!driven.some(moving)) { last = null; return; }
    waiting = requestAnimationFrame(tick);
  };
  const tick = (now) => {
    waiting = 0;
    if (!live) return;
    // The first frame after a wait takes one step.
    if (last === null) last = now - METER_STEP_MS;
    let steps = Math.round((now - last) / METER_STEP_MS);
    if (steps > METER_MAX_STEPS) { steps = METER_MAX_STEPS; last = now - steps * METER_STEP_MS; }
    for (const d of driven)
      for (let i = 0; i < steps && moving(d); i += 1) {
        d.shown = d.shown.map((s, k) => meterStep(d.meter.face, s, d.target[k]));
        setMeter(d, d.shown);
      }
    last += steps * METER_STEP_MS;
    wake();
  };
  // A meter that goes on from the last drawing's may still be on its way.
  wake();
  return {
    elements: driven.map((d) => d.el),
    /** Where each meter stands now, 0 to 1 for each column, by its id: for the plugin's own checks. */
    levels() {
      const out = {};
      for (const d of driven) out[d.meter.id] = d.shown.slice();
      return out;
    },
    show(values) {
      const sent = values && typeof values === "object" ? values : {};
      for (const d of driven)
        d.target = d.meter.ids.map((id) => {
          const value = Object.prototype.hasOwnProperty.call(sent, id) ? sent[id] : null;
          return typeof value === "number" && !Number.isNaN(value) ? meterLevel(d.meter, value) : 0;
        });
      wake();
    },
    stop() {
      live = false;
      if (waiting) cancelAnimationFrame(waiting);
      waiting = 0;
      if (!driven.length) return null;
      const stood = {};
      for (const d of driven)
        stood[d.meter.id] = {
          face: d.meter.face, stereo: d.meter.stereo,
          shown: d.shown.slice(), target: d.target.slice(), peak: d.peak.slice(),
        };
      return stood;
    },
  };
}

/** Sets a driven meter's face, its columns at `levels`, and keeps the bar's peak lines as the library does. */
function setMeter(d, levels) {
  if (d.n === 2) d.el.setLevel(levels[0], levels[1]);
  else d.el.setLevel(levels[0]);
  // The library's own sum for the bar's peak line, kept here to know when the line has landed.
  if (d.bar) d.peak = d.peak.map((p, k) => Math.max(levels[k], p - BAR_PEAK_FALL));
}

/**
 * Draws a panel with the library (Pangea.render), and hands its meters to the host (decisions 76
 * and 82). `panel` is buildPanel's. Meters are asked for here and nowhere else, in the same call
 * that starts their driver: the spec the library is given is drawnSpec's, with every meter in its
 * section, and whatever was drawn, meterDriver stops the library's animation and moves the meters
 * from the host's values only, beginning at rest, or where the last drawing's stood when `from`
 * says they are the same. A page that draws with Pangea.render itself gets no meter, so no page
 * can show one that nothing drives. Answers `ctx`, what Pangea.render answers, and `meter`, the
 * driver: give its `show` the host's values, call its `stop` before the next drawing, and give
 * what that answers to the next drawing as `from`.
 */
export function drawPanel(root, panel, from) {
  const ctx = window.Pangea.render(root, drawnSpec(panel), panel.params);
  return { ctx, meter: meterDriver(root, panel, from, ctx), stage: stageDriver(panel, ctx) };
}

// ---------- the chord map and the notes (docs/felt-look.md) ----------

/** How many seconds of notes the roll shows. */
export const ROLL_SECONDS = 8;
/** How often the chord map reads the notes, in milliseconds. */
export const HARMONY_MS = 250;
/**
 * How many seconds of notes the stage keeps: the twelve the key is read from (harmony.js, keyNow)
 * and one more. Older notes are forgotten as notes arrive and on the chord map's tick, whether
 * or not a roll is drawn, so nothing grows behind a covered window (the review of 19 November).
 */
export const KEEP_SECONDS = 13;

/**
 * Drives a drawing's chord map and notes from the notes its host sends (`notes(events)`, as
 * [on, note, velocity, channel] fours, heard now). The chord and the key are read from them four
 * times a second (./harmony.js), with the tool's own Key, Mode and Chord menus where it has them;
 * the roll is drawn each frame while it has notes to show, and not otherwise. A press on a chord
 * sets the tool's Chord menu to it, where the tool has one, and does nothing where it has none.
 * `quiet()` ends what sounds (the host stopped, or bypassed the tool); `stop()` lets the drawing go.
 * `clock` is the time in seconds. A drawing with neither part answers a driver that does nothing.
 */
export function stageDriver(panel, ctx, clock) {
  const parts = ctx && ctx.parts ? ctx.parts : {};
  const map = parts.chord_map || null, roll = parts.note_roll || null;
  const now = clock || (() => (typeof performance !== "undefined" ? performance.now() : Date.now()) / 1000);
  if (!map && !roll) return { notes() {}, quiet() {}, stop() {}, book: null, harmony: () => null };
  const book = new NotesBook();
  const raf = typeof requestAnimationFrame === "function" ? requestAnimationFrame : null;
  const menus = (panel.harmony && panel.harmony.menus) || {};
  let shown = null, frame = 0, stopped = false;
  // The label a menu shows now. A Chord menu the map sets is read by its first chord, its home,
  // so the ring stays where it is while the player chooses chords on it.
  const chordMenu = panel.harmony && panel.harmony.chord;
  const chosen = (what) => {
    const id = menus[what];
    const p = id ? panel.byId[id] : null;
    if (!p || !p.choices) return null;
    if (what === "chord" && id === chordMenu) return p.choices[0] ?? null;
    const at = Math.round(ctx.n(id) * Math.max(0, p.choices.length - 1));
    return p.choices[at] ?? null;
  };
  const think = () => {
    if (stopped) return;
    const t = now();
    book.prune(t - KEEP_SECONDS);
    shown = harmonyNow(chosen, book, t, shown);
    if (map) map.show(Object.assign({}, shown, { playing: book.notesIn(t - ROLL_SECONDS, t).length > 0 }));
  };
  const draw = () => {
    frame = 0;
    if (stopped || !roll) return;
    const t = now();
    const list = book.notesIn(t - ROLL_SECONDS, t);
    book.prune(t - KEEP_SECONDS);
    roll.draw(list, t, ROLL_SECONDS, (n) => noteName(n, shown && shown.key), "Press Play: the notes appear here");
    if (list.length > 0 && raf) frame = raf(draw);
  };
  const timer = setInterval(think, HARMONY_MS);
  think();
  draw();
  if (map && chordMenu) {
    ctx.onChoose = (degree, state) => {
      const entry = state && Array.isArray(state.ring) ? state.ring.find((r) => r.number === degree) : null;
      const p = panel.byId[chordMenu];
      if (!entry || !p || !p.choices) return;
      const minor = /^[A-G][#b]?m(?!aj)/.test(entry.name);
      const same = p.choices.map((c, i) => ({ c: String(c), i })).filter((x) => parseTonic(x.c) === entry.pitches[0]);
      const pick = same.find((x) => /^\s*[A-Ga-g][#b♯♭]?m(?!aj)/.test(x.c) === minor) || same[0];
      if (pick) ctx.user(chordMenu, p.choices.length > 1 ? pick.i / (p.choices.length - 1) : 0);
    };
  }
  return {
    notes(events) {
      if (stopped || !Array.isArray(events)) return;
      const t = now();
      book.add(events, t);
      book.prune(t - KEEP_SECONDS);
      if (roll && !frame) {
        if (raf) frame = raf(draw);
        else draw();
      }
    },
    quiet() {
      book.endAll(now());
      think();
    },
    stop() {
      stopped = true;
      clearInterval(timer);
      if (frame && typeof cancelAnimationFrame === "function") cancelAnimationFrame(frame);
      frame = 0;
    },
    book,
    harmony: () => shown,
  };
}

/**
 * The spec drawPanel gives the library: buildPanel's, with the meters when buildPanel answered
 * any. Each is listed in `meters`, bound to the host by its id, and named last in its section's
 * controls, so the library packs it there beside them. A page that draws with Pangea.render itself
 * gives the library `panel.spec`, with no meter.
 */
export function drawnSpec(panel) {
  if (!Array.isArray(panel.meters) || panel.meters.length === 0) return panel.spec;
  return Object.assign({}, panel.spec, {
    sections: panel.metered,
    meters: panel.meters.map((m) => ({ id: m.id, type: m.face, label: m.label, size: m.size, stereo: m.stereo })),
  });
}

// ---------- fitting a planned panel to its pages (decision 79) ----------

/**
 * How many pages a drawing has. The library lays a panel out on pages of a fixed number of grid
 * rows, and when the sections need more than one page it draws a row of tabs, one for each. So a
 * drawing with no tabs is one page, and one with tabs has a page for each tab.
 */
export const pagesIn = (root) => root.querySelectorAll(".pg-tab").length || 1;

/**
 * How many pages the library needs for `spec` and `params`, drawn into an element that is never
 * on the page, so nobody sees it. The library packs a panel by fixed footprints on its own grid,
 * and never measures the screen, so the count is exact and the same wherever the panel is drawn.
 * What the drawing started is let go: the library's watch on its size, and the animation of any
 * part it animates, which is stopped and taken out of the drawing.
 * `library` is the library (Pangea).
 */
export function countPages(library, spec, params) {
  const box = document.createElement("div");
  library.render(box, spec, params);
  const pages = pagesIn(box);
  if (box._ro && typeof box._ro.disconnect === "function") box._ro.disconnect();
  for (const part of [".pg-meter", ".pg-disp", ".pg-steps"])
    for (const el of box.querySelectorAll(part)) {
      if (typeof el.frame === "function") el.frame = still;
      el.remove();
    }
  box.replaceChildren();
  return pages;
}

/** An arrangement with each control's entry changed by `change`, which may drop its size. */
const resized = (arranged, change) => {
  const controls = {};
  const entries = arranged.controls && typeof arranged.controls === "object" ? arranged.controls : {};
  for (const key of Object.keys(entries)) {
    const e = entries[key];
    if (!e || typeof e !== "object") continue;
    const next = Object.assign({}, e);
    const size = change(e.size);
    if (size === "s" || size === "m" || size === "l") next.size = size;
    else delete next.size;
    controls[key] = next;
  }
  return Object.assign({}, arranged, { controls });
};

/**
 * The fit's ladder, tried in order until a drawing needs one page (decision 90). Each step names
 * what it does to the planner's arrangement. Only sizes, the layout family and the hero change:
 * the sections, their order, the labels and what each control is drawn as stay. The merge of
 * sections (MERGE_STEP) comes after the ladder, and only for 24 controls or fewer.
 *
 * - `plan`: the plan as it is.
 * - `medium_small`: every medium control small.
 * - `one_smaller`: as `medium_small`, and every large control medium.
 * - `library_sizes`: no size in the arrangement, so the library chooses each size itself, as it
 *   does for the automatic panel (a size the code marks with [size:s] or [size:l] stays).
 * - `look_family`: as `library_sizes`, with the look's own layout family and no hero named.
 */
export const FIT_STEPS = [
  { step: "plan", make: (a) => a },
  { step: "medium_small", make: (a) => resized(a, (s) => (s === "m" ? "s" : s)) },
  { step: "one_smaller", make: (a) => resized(a, (s) => (s === "m" ? "s" : s === "l" ? "m" : s)) },
  { step: "library_sizes", make: (a) => resized(a, () => null) },
  {
    step: "look_family",
    make: (a) => {
      const next = resized(a, () => null);
      delete next.layout;
      delete next.hero;
      return next;
    },
  },
];

/** `data` with `arrangement` in its ui, or with none when it is null. */
const withArrangement = (data, arrangement) => {
  const ui = Object.assign({}, data.ui || {});
  if (arrangement) ui.arrangement = arrangement;
  else delete ui.arrangement;
  return Object.assign({}, data, { ui });
};

/**
 * The owners' UI guidelines, R1 and R2: what a typical use needs fits in view, and no tabs unless a
 * panel has more than 24 controls, and then at most two pages.
 */
export const ONE_PAGE_CONTROLS = 24;
export const MOST_PAGES = 2;

/** The fit's last step, after the ladder: the smallest sections merged into their neighbours. */
export const MERGE_STEP = "merge_sections";

/**
 * The most pages a planned panel may need (decision 90): one with 24 controls or fewer, two above
 * that (R1 and R2), and never more than `automatic`, the pages of the same plugin's panel with no
 * plan. The fit aims at one page whatever the count: R2 allows tabs above 24 controls, and R1
 * still asks for what a typical use needs to be in view.
 */
export const fitTarget = (controls, automatic) =>
  Math.min(controls <= ONE_PAGE_CONTROLS ? 1 : MOST_PAGES, automatic);

/** The words of a section's name: "Mix and Output" is Mix and Output, "A, B and C" three. */
const namesIn = (name) => name.split(/, | and /).filter((part) => part.length > 0);

/**
 * The name of two sections merged, in panel order: "Filter and Sequencer", or "Input, Drive and
 * Filter" when one was merged before. Longer than a section's name may be (40 characters), it is
 * the name of the section that held more controls, the first of a tie.
 */
export const mergedName = (first, second) => {
  const parts = [...namesIn(first.name), ...namesIn(second.name)];
  const joined =
    parts.length > 1 ? parts.slice(0, -1).join(", ") + " and " + parts[parts.length - 1] : parts.join("");
  if (joined.length > 0 && joined.length <= 40) return joined;
  return second.controls.length > first.controls.length ? second.name : first.name;
};

/**
 * The arrangement with its smallest section merged into a neighbour, or null when it has one
 * section or none. The smallest holds the fewest controls, the first of a tie. It goes into the
 * smaller of its two neighbours, the one before it in a tie, or into its only one. The controls
 * keep their order, and nothing else changes: labels, sizes, what each is drawn as, the family.
 */
export function mergeSmallest(arranged) {
  const sections = Array.isArray(arranged.sections) ? arranged.sections.filter((x) => x && Array.isArray(x.controls)) : [];
  if (sections.length < 2) return null;
  let at = 0;
  sections.forEach((sec, i) => {
    if (sec.controls.length < sections[at].controls.length) at = i;
  });
  const before = at > 0 ? sections[at - 1] : null;
  const after = at < sections.length - 1 ? sections[at + 1] : null;
  const into = !after || (before && before.controls.length <= after.controls.length) ? at - 1 : at + 1;
  const first = sections[Math.min(at, into)];
  const second = sections[Math.max(at, into)];
  const merged = { name: mergedName(first, second), controls: [...first.controls, ...second.controls] };
  const next = sections.slice();
  next.splice(Math.min(at, into), 2, merged);
  return Object.assign({}, arranged, { sections: next });
}

/**
 * buildPanel, and for an arrangement the panel planner made (`madeBy: "planner"`) the fit. The
 * planner chooses sizes and cannot see how the library packs a panel, and in its first live run
 * six of 25 planned panels needed more pages than the same plugin's panel with no plan: a large
 * control is much larger. So a planned panel is fitted to the owners' UI guidelines, R1 and R2
 * (decision 90): what a typical use needs fits in view, and no tabs unless there are more than 24
 * controls, and then at most two. The ladder (FIT_STEPS) is tried in order until a drawing needs
 * one page, and the step with the fewest pages is used, the earliest of those that tie. So a plan
 * that needs one page is drawn as it is, and sizes are made smaller only as far as one page needs.
 *
 * Only when the ladder cannot bring a panel of 24 controls or fewer to one page, one last step
 * (`merge_sections`) merges the smallest section into a neighbour (mergeSmallest), one at a time,
 * from the ladder's best drawing, until one page is enough or one section is left. A merge is
 * kept only when it needs fewer pages than the best drawing before it.
 *
 * `pagesOf(panel)` answers how many pages a panel needs: countPages with the spec the page will
 * draw. A person's arrangement, and a panel with none, is built as it is, with no extra drawing:
 * a person's sizes are never changed (decision 79).
 *
 * Answers `panel`, buildPanel's for the arrangement drawn, and `fit`: null when no fit ran, and
 * otherwise `{ step, pages, target, arrangement }`: the step used, its pages, the most pages the
 * guidelines allow it (fitTarget), and the arrangement drawn, which Edit layout starts from.
 */
export function fitPanel(data, pagesOf) {
  const arranged = data.ui && data.ui.arrangement;
  if (!arranged || typeof arranged !== "object" || arranged.madeBy !== "planner") {
    return { panel: buildPanel(data), fit: null };
  }
  // A plan with roles (decision 97) is fitted from the sizes its roles give, whatever the look.
  const entries = arranged.controls && typeof arranged.controls === "object" ? Object.values(arranged.controls) : [];
  const roled = entries.some((e) => e && typeof e === "object" && CONTROL_ROLES.includes(e.role));
  if (lookOf(data.ui) || roled) {
    const fitted = fitLookPanel(data, arranged, pagesOf);
    // The planner's components (docs/panel-planner.md, change 6) never cost a page, nor the
    // sizes of the controls: when its fit needs more than one page, or had to make controls smaller
    // than the plan's (any step after "plan"), the same fit with the code's own components is
    // tried, and drawn when it needs fewer pages, or as many with larger controls (an earlier step
    // of LOOK_FIT_ORDER). So a fader is drawn only where it costs neither.
    if (
      (fitted.fit.pages > 1 || fitted.fit.step !== "plan") &&
      entries.some((e) => e && typeof e === "object" && typeof e.prefer === "string")
    ) {
      const plain = fitLookPanel(data, withoutPreferences(arranged), pagesOf);
      const earlier = LOOK_FIT_ORDER.indexOf(plain.fit.step) < LOOK_FIT_ORDER.indexOf(fitted.fit.step);
      if (plain.fit.pages < fitted.fit.pages || (plain.fit.pages === fitted.fit.pages && earlier)) return plain;
    }
    return fitted;
  }
  const automatic = buildPanel(withArrangement(data, null));
  const controls = automatic.sections.reduce((n, s) => n + s.controls.length, 0);
  const target = fitTarget(controls, pagesOf(automatic));
  let best = null;
  for (const { step, make } of FIT_STEPS) {
    const arrangement = make(arranged);
    const panel = buildPanel(withArrangement(data, arrangement));
    const pages = pagesOf(panel);
    if (!best || pages < best.pages) best = { step, pages, panel, arrangement };
    if (pages <= 1) break;
  }
  if (best.pages > 1 && controls <= ONE_PAGE_CONTROLS) {
    let arrangement = best.arrangement;
    for (;;) {
      arrangement = mergeSmallest(arrangement);
      if (!arrangement) break;
      const panel = buildPanel(withArrangement(data, arrangement));
      const pages = pagesOf(panel);
      if (pages < best.pages) best = { step: MERGE_STEP, pages, panel, arrangement };
      if (pages <= 1) break;
    }
  }
  return {
    panel: best.panel,
    fit: { step: best.step, pages: best.pages, target, arrangement: best.arrangement },
  };
}

// ---------- fitting a planned panel with a look (decision 97) ----------

/**
 * The arrangement with the size each control was drawn at written into its entry, for a control
 * whose role chose its size (sizeForRole), so that the fit's steps can make it smaller. `panel` is
 * buildPanel's for `arranged`. A size an entry already has, and a control drawn at the library's
 * own size, stay as they are.
 */
export function withDrawnSizes(arranged, panel) {
  const controls = {};
  const entries = arranged.controls && typeof arranged.controls === "object" ? arranged.controls : {};
  const byKey = {};
  for (const p of panel.params) byKey[p.key] = p;
  for (const key of Object.keys(entries)) {
    const e = entries[key];
    if (!e || typeof e !== "object") continue;
    const next = Object.assign({}, e);
    const p = byKey[key];
    const drawn = p && panel.spec.arrangement[p.id];
    if (!next.size && next.role && drawn && drawn.size) next.size = drawn.size;
    controls[key] = next;
  }
  return Object.assign({}, arranged, { controls });
}

/**
 * The fit's size steps for a planned panel with a look, from the sizes it was drawn at (decision
 * 97): as planned, then every medium control small, then also every large control medium, then
 * every control at the library's own size for the family. Only sizes change.
 */
export const LOOK_SIZE_STEPS = [
  { step: "plan", make: (a) => a },
  { step: "medium_small", make: (a) => resized(a, (s) => (s === "m" ? "s" : s)) },
  { step: "one_smaller", make: (a) => resized(a, (s) => (s === "m" ? "s" : s === "l" ? "m" : s)) },
  {
    step: "library_sizes",
    make: (a, panel) => {
      const family = panel.spec.layout;
      const count = panel.sections.reduce((n, s) => n + s.controls.length, 0);
      const room = (LIBRARY_SIZES[family] || LIBRARY_SIZES.flat_panel)(count);
      const heroId = panel.spec.hero && panel.spec.hero.control;
      const controls = {};
      for (const key of Object.keys(a.controls || {})) {
        const e = Object.assign({}, a.controls[key]);
        const p = panel.params.find((x) => x.key === key);
        if (e.role) e.size = p && p.id === heroId ? SIZE_UP[room] : room;
        controls[key] = e;
      }
      return Object.assign({}, a, { controls });
    },
  },
];

/** The fit's step that moves a stage family's panel to a flat panel (decision 97). */
export const FLAT_STEP = "flat_panel";
/** The fit's step that draws each control as its most compact component (decision 97). */
export const COMPACT_STEP = "compact";
/** The most compact component of each wide one the code or the library may draw. */
const COMPACT = { fader: "knob", fader_h: "knob", segmented: "dropdown", stepped_rotary: "dropdown" };

/**
 * The arrangement with every control the drawing shows as a wide component given its most compact
 * one, the last of the smaller controls (decision 97): a fader a knob, a segmented choice or a
 * rotary a dropdown, a switch drawn as segmented buttons a plain toggle. Only the planner's
 * controls change, those with a role and no component of their own; a person's never.
 */
export function compactComponents(arranged, panel) {
  const controls = {};
  const entries = arranged.controls && typeof arranged.controls === "object" ? arranged.controls : {};
  const byKey = {};
  for (const p of panel.params) byKey[p.key] = p;
  for (const key of Object.keys(entries)) {
    const e = entries[key];
    if (!e || typeof e !== "object") continue;
    const next = Object.assign({}, e);
    const p = byKey[key];
    if (p && e.role && !e.as) {
      // What the control is drawn as: its arrangement's component, or the library's own for its kind.
      const set = (panel.spec.arrangement[p.id] || {}).as;
      const drawn =
        set ||
        (p.kind === "toggle"
          ? panel.spec.switch_style
          : p.kind === "choice"
            ? libraryChoice(p.choices.length, panel.spec.switch_style, panel.spec.selector_style)
            : null);
      if (p.kind === "toggle" && drawn === "segmented") next.as = "toggle";
      else if (drawn && COMPACT[drawn] && (DRAWN_AS[p.kind] || []).includes(COMPACT[drawn])) next.as = COMPACT[drawn];
    }
    controls[key] = next;
  }
  return Object.assign({}, arranged, { controls });
}
/** The most controls a section may hold for the fit to merge it into a neighbour. */
export const MERGE_MOST = 2;

/**
 * The arrangement with its smallest section of one or two controls merged into a neighbour, as
 * mergeSmallest merges, or null when no section that small is left beside another.
 */
export function mergeSmallSection(arranged) {
  const sections = Array.isArray(arranged.sections) ? arranged.sections.filter((x) => x && Array.isArray(x.controls)) : [];
  if (sections.length < 2) return null;
  const smallest = Math.min(...sections.map((s) => s.controls.length));
  if (smallest > MERGE_MOST) return null;
  return mergeSmallest(Object.assign({}, arranged, { sections }));
}

/**
 * Tries the size steps on `arranged`, then its smallest drawing with every wide component its most
 * compact one (COMPACT_STEP), and answers `best`, the drawing with the fewest pages (the earliest of
 * a tie, so the largest controls that fit), and `smallest`, the last drawing tried, which the next
 * steps of the fit start from.
 */
const sizeLadder = (data, arranged, pagesOf, step) => {
  const first = buildPanel(withArrangement(data, arranged));
  const sized = withDrawnSizes(arranged, first);
  let best = null;
  let smallest = null;
  for (const s of LOOK_SIZE_STEPS) {
    const arrangement = s.make(sized, first);
    const panel = buildPanel(withArrangement(data, arrangement));
    const pages = pagesOf(panel);
    smallest = { step: step || s.step, pages, panel, arrangement };
    if (!best || pages < best.pages) best = smallest;
    if (pages <= 1) return { best, smallest };
  }
  const arrangement = compactComponents(smallest.arrangement, smallest.panel);
  const panel = buildPanel(withArrangement(data, arrangement));
  const pages = pagesOf(panel);
  smallest = { step: step || COMPACT_STEP, pages, panel, arrangement };
  if (pages < best.pages) best = smallest;
  return { best, smallest };
};

/**
 * The fit of a planned panel with a look (decision 97), in the order the owners' brief of 17 November
 * gives: smaller controls (LOOK_SIZE_STEPS, then COMPACT_STEP); then sections of one or two controls
 * merged into a neighbour, one at a time, whatever the number of controls; then, for a hero_knob or a
 * display_first panel, a flat panel, with the smaller controls again. Each step starts from the
 * smallest drawing before it, and the fit stops at the first drawing that needs one page. Otherwise
 * it keeps the drawing with the fewest pages, the earliest of a tie. The look is never changed, only
 * the sizes, the components, the sections and, last, the family. A panel that still needs more than
 * two pages is kept as the best there is, and its page says so (builder.js and host.js log it).
 */
function fitLookPanel(data, arranged, pagesOf) {
  const automatic = buildPanel(withArrangement(data, null));
  const controls = automatic.sections.reduce((n, s) => n + s.controls.length, 0);
  const target = fitTarget(controls, pagesOf(automatic));
  const done = (best) => ({
    panel: best.panel,
    fit: { step: best.step, pages: best.pages, target, arrangement: best.arrangement },
  });
  const ladder = sizeLadder(data, arranged, pagesOf);
  let best = ladder.best;
  let from = ladder.smallest;
  if (best.pages <= 1) return done(best);
  for (let arrangement = mergeSmallSection(from.arrangement); arrangement; arrangement = mergeSmallSection(arrangement)) {
    const panel = buildPanel(withArrangement(data, arrangement));
    const pages = pagesOf(panel);
    from = { step: MERGE_STEP, pages, panel, arrangement };
    if (pages < best.pages) best = from;
    if (pages <= 1) return done(best);
  }
  if (STAGE_FAMILIES.includes(from.panel.spec.layout)) {
    // The flat panel starts from the merged sections, with each role's own size for the new
    // family: the sizes the steps chose were for the stage family's room.
    const flat = Object.assign({}, resized(from.arrangement, () => null), { layout: "flat_panel" });
    for (const key of Object.keys(flat.controls)) {
      const before = from.arrangement.controls[key];
      if (before && before.size && !before.role) flat.controls[key].size = before.size;
      if (before && before.as && !before.role) flat.controls[key].as = before.as;
      else delete flat.controls[key].as;
    }
    const tried = sizeLadder(data, flat, pagesOf, FLAT_STEP);
    if (tried.best.pages < best.pages) best = tried.best;
  }
  return done(best);
}

/**
 * The steps of a planned panel's fit with a look, from the plan's own sizes to the most it changes:
 * smaller controls, compact components, merged sections, a flat panel (fitLookPanel).
 */
export const LOOK_FIT_ORDER = ["plan", "medium_small", "one_smaller", "library_sizes", "compact", "merge_sections", "flat_panel"];

/** The arrangement with no component the planner preferred (change 6): the code chooses each. */
export function withoutPreferences(arranged) {
  const controls = {};
  const entries = arranged.controls && typeof arranged.controls === "object" ? arranged.controls : {};
  for (const key of Object.keys(entries)) {
    const e = entries[key];
    if (!e || typeof e !== "object") continue;
    const { prefer: _prefer, ...rest } = e;
    controls[key] = rest;
  }
  return Object.assign({}, arranged, { controls });
}

// ---------- what was drawn (docs/panel-planner.md, change 7) ----------

/** The most fallbacks a drawing lists; any more are counted (`moreFallbacks`). */
export const DRAWN_FALLBACKS_MOST = 128;

/**
 * What the library draws a control as, by its own rules (pangea.js: `pick`, then `ctx.ctl`, which
 * draws a segmented, dropdown or rotary control of the wrong kind as a knob or a toggle), in the
 * library's names, with a vertical fader as "fader". A test holds this to pangea.js.
 */
export function drawnComponent(p, spec) {
  const a = (spec.arrangement || {})[p.id] || {};
  const S = Object.assign({ switch_style: "toggle", selector_style: "dropdown" }, spec);
  let type;
  if (a.as) type = a.as;
  else if (p.tempo) type = "tempo_sync";
  else if (p.kind === "momentary") type = "lit_button";
  else if (p.kind === "toggle") type = S.switch_style === "segmented" ? "segmented" : S.switch_style;
  else if (p.kind === "choice")
    type = S.switch_style === "segmented" && p.choices.length <= 4 ? "segmented" : S.selector_style === "tempo_sync" ? "dropdown" : S.selector_style;
  else if (p.kind === "stepped") type = S.selector_style === "number_box" ? "number_box" : "knob";
  else type = "knob";
  if (type === "segmented" && p.kind !== "toggle" && p.kind !== "choice") type = "knob";
  if ((type === "dropdown" || type === "stepped_rotary") && p.kind !== "choice") type = p.kind === "toggle" ? "toggle" : "knob";
  return type === "fader_v" ? "fader" : type;
}

/**
 * What a drawing shows, for the site to keep with the version so that the variety of panels can
 * be measured (docs/panel-planner.md, change 7): the family, the pages, how many controls
 * each component and each size drew, the display, and each component the planner preferred
 * (`arranged`, the arrangement as stored) that was drawn otherwise, with why: "kind", "names" or
 * "layout" when the code could not draw it (plannedComponent), "stage" for the hero of a stage,
 * and "fit" when the fit drew a more compact one so that the panel needs fewer pages; and, when
 * the fit ran (`fit`), its step and the pages it aimed at. The hero of
 * a hero_knob panel is a knob and of a display_first panel a fader, both large, as the library
 * draws them.
 */
export function drawnOf(panel, arranged, pages, fit) {
  const S = panel.spec;
  const family = S.layout;
  const ids = panel.sections.flatMap((s) => s.controls).filter((id) => panel.byId[id]);
  let hero = S.hero && S.hero.control;
  if (family === "hero_knob" && !hero) hero = ids[0];
  const room = (LIBRARY_SIZES[family] || LIBRARY_SIZES.flat_panel)(family === "hero_knob" ? ids.length - 1 : ids.length);
  const components = {}, sizes = {}, fallbacks = [];
  const entries = arranged && typeof arranged === "object" && arranged.controls && typeof arranged.controls === "object" ? arranged.controls : {};
  for (const id of ids) {
    const p = panel.byId[id];
    const staged = id === hero && STAGE_FAMILIES.includes(family);
    const as = staged ? (family === "hero_knob" ? "knob" : "fader") : drawnComponent(p, S);
    const a = (S.arrangement || {})[id] || {};
    const size = staged ? "l" : a.size || ((family === "flat_panel" || family === "hardware_unit") && id === hero ? SIZE_UP[room] : room);
    components[as] = (components[as] || 0) + 1;
    sizes[size] = (sizes[size] || 0) + 1;
    const e = entries[p.key];
    const wanted = e && typeof e === "object" && typeof e.prefer === "string" ? e.prefer : null;
    if (wanted && wanted !== as) {
      const why = staged ? "stage" : (panel.refused && panel.refused[id]) || "fit";
      fallbacks.push({ key: p.key, wanted, drawn: as, why });
    }
  }
  return {
    family,
    pages,
    step: fit ? fit.step : null,
    target: fit ? fit.target : null,
    controls: ids.length,
    components,
    sizes,
    display: S.display ? S.display.type : null,
    // At most DRAWN_FALLBACKS_MOST are listed; the rest are counted (decision 109).
    fallbacks: fallbacks.slice(0, DRAWN_FALLBACKS_MOST),
    moreFallbacks: Math.max(0, fallbacks.length - DRAWN_FALLBACKS_MOST),
  };
}

/** A control's real value as the library's 0 to 1. */
export const toN = (p, v) => {
  if (p.values) return p.values.length > 1 ? Math.max(0, p.values.indexOf(v)) / (p.values.length - 1) : 0;
  const lo = p.min, hi = p.max;
  if (p.scale === "log") return Math.log(v / lo) / Math.log(hi / lo);
  return hi > lo ? (v - lo) / (hi - lo) : 0;
};
