// Plays the native harness's scripts through the WebAssembly build of Two Hands, through the
// ABI in docs/midi-tools.md ("WebAssembly"), and compares every event with the native one, to
// the sample. The arithmetic of the host follows test/runner.h step for step.
//
//   node shell/examples/two-hands/test/wasm-compare.mjs <logic.wasm> <dump from harness --dump>
//
// Exit 0 when every event matches.

import { readFileSync } from "node:fs";

const [wasmPath, dumpPath] = process.argv.slice(2);
if (!wasmPath || !dumpPath) {
  console.log("usage: wasm-compare.mjs <logic.wasm> <dump>");
  process.exit(2);
}

const compiled = await WebAssembly.compile(readFileSync(wasmPath));
const imports = WebAssembly.Module.imports(compiled);
if (imports.length > 0) {
  console.log(`FAIL the module asks for ${imports.length} imports`);
  process.exit(1);
}
const instance = await WebAssembly.instantiate(compiled, {});
const x = instance.exports;
if (typeof x.__wasm_call_ctors === "function") x.__wasm_call_ctors();
const EVENT = x.event_size();
if (x.api_version() !== 2 || EVENT !== 20) {
  console.log(`FAIL API ${x.api_version()}, event size ${EVENT}`);
  process.exit(1);
}
const numParams = x.num_params();

// The dump: S name sr bpm bpb block startPpq length, then steps, then E lines, then X.
function parse(text) {
  const scripts = [];
  let current = null;
  for (const line of text.split("\n")) {
    if (line === "") continue;
    const [kind, ...rest] = line.split(" ");
    if (kind === "S") {
      const [name, sr, bpm, bpb, block, startPpq, length] = rest;
      current = {
        name,
        sampleRate: Number(sr),
        bpm: Number(bpm),
        beatsPerBar: Number(bpb),
        block: Number(block),
        startPpq: Number(startPpq),
        length: Number(length),
        steps: [],
        events: [],
      };
    } else if (kind === "E") {
      const [time, control, note, channel, value] = rest.map(Number);
      current.events.push({ time, control, note, channel, value });
    } else if (kind === "X") {
      scripts.push(current);
      current = null;
    } else {
      const [at, index, value] = rest;
      current.steps.push({ kind, at: Number(at), index: Number(index), value: Number(value) });
    }
  }
  return scripts;
}

function run(s) {
  x.prepare(s.sampleRate);
  const events = [];
  const applied = s.steps.map(() => false);
  let bpm = s.bpm;
  let spb = (s.sampleRate * 60.0) / bpm;
  let ppqBase = s.startPpq;
  let posBase = 0;
  let pos = 0;

  const call = (n, playing) => {
    const ppq = ppqBase + (pos - posBase) / spb;
    const count = x.process(s.sampleRate, bpm, ppq, n, playing ? 1 : 0, s.beatsPerBar);
    const view = new DataView(x.memory.buffer);
    const at = x.events_ptr();
    for (let i = 0; i < count; i++) {
      const o = at + i * EVENT;
      events.push({
        time: pos + view.getInt32(o, true),
        control: view.getInt32(o + 4, true),
        value: view.getFloat32(o + 8, true),
        note: view.getInt32(o + 12, true),
        channel: view.getInt32(o + 16, true),
      });
    }
  };
  const applySteps = () => {
    s.steps.forEach((st, i) => {
      if (applied[i] || st.at > pos) return;
      applied[i] = true;
      if (st.kind === "P") {
        new Float32Array(x.memory.buffer, x.params_ptr(), numParams)[st.index] = st.value;
      } else if (st.kind === "J") {
        ppqBase = st.value;
        posBase = pos;
      } else if (st.kind === "T") {
        ppqBase = ppqBase + (pos - posBase) / spb;
        posBase = pos;
        bpm = st.value;
        spb = (s.sampleRate * 60.0) / bpm;
      } else if (st.kind === "H") {
        for (let k = 0; k < st.value; k++) call(s.block, false);
      }
    });
  };

  applySteps();
  call(s.block, false);
  while (pos < s.length) {
    applySteps();
    const n = Math.min(s.block, s.length - pos);
    call(n, true);
    pos += n;
  }
  applySteps();
  call(s.block, false);
  return events;
}

const scripts = parse(readFileSync(dumpPath, "utf8"));
let failed = 0;
let events = 0;
let notes = 0;
for (const s of scripts) {
  const ours = run(s);
  const theirs = s.events;
  let problem = ours.length === theirs.length ? "" : `${ours.length} events against ${theirs.length}`;
  for (let i = 0; !problem && i < ours.length; i++) {
    const a = ours[i];
    const b = theirs[i];
    if (
      a.time !== b.time ||
      a.control !== b.control ||
      a.note !== b.note ||
      a.channel !== b.channel ||
      a.value !== Math.fround(b.value)
    )
      problem = `event ${i}: wasm ${JSON.stringify(a)}, native ${JSON.stringify(b)}`;
  }
  if (problem) {
    failed++;
    console.log(`  FAIL ${s.name}: ${problem}`);
  }
  events += theirs.length;
  notes += theirs.filter((e) => e.control === -1).length;
}
console.log(
  `WebAssembly against native: ${scripts.length - failed} of ${scripts.length} scripts identical, ` +
    `${events} events (${notes} notes) compared to the sample`,
);
process.exit(failed === 0 ? 0 : 1);
