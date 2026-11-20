// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// The events mirror end to end: the host simulator (--serve) is the FM-1, played by a script of hands; the browser
// build is the viewer (web/emu/live.html's part, in Node). The viewer takes the FM-1's state, then its events, and
// its own state's signature must match the FM-1's after every step (pages, knobs, a held chord, MIDI in).
//   node tests/host/live_test.mjs HOST_BIN APP_WASM
import fs from "fs";
import { spawn } from "child_process";
import readline from "readline";

const [hostBin, wasmPath] = process.argv.slice(2);
const MR_REQ = 0x50, MR_INPUT = 0x53, MR_MIDI = 0x55, MR_SIG = 0x56, MR_STATE = 0x57, MR_NIN = 27;
const pack7 = (bytes) => { const o = []; let a = 0, n = 0; for (const b of bytes) { a |= b << n; n += 8; while (n >= 7) { o.push(a & 127); a >>= 7; n -= 7; } } if (n) o.push(a & 127); return o; };
const unpack7 = (src) => { const o = []; let a = 0, n = 0; for (const b of src) { a |= (b & 127) << n; n += 7; if (n >= 8) { o.push(a & 255); a >>= 8; n -= 8; } } return o; };
const hex = (u8) => Buffer.from(u8).toString("hex").toUpperCase();
const request = (op) => hex([0xF0, ...pack7([0, 0x59, MR_REQ, 1, 0, 0, op, ~op & 255]), 0xF7]);

// the viewer
let mem;
const wasi = new Proxy({ clock_time_get: (id, p, out) => { new DataView(mem.buffer).setBigUint64(out, 0n, true); return 0; },
  fd_write: (fd, iov, n, out) => { new DataView(mem.buffer).setUint32(out, 0, true); return 0; } }, { get: (t, k) => k in t ? t[k] : () => 0 });
const { instance } = await WebAssembly.instantiate(fs.readFileSync(wasmPath), { wasi_snapshot_preview1: wasi, env: new Proxy({}, { get: () => () => 0 }) });
const ex = instance.exports; mem = ex.memory;
if (ex._initialize) ex._initialize();
ex.web_master(2800); ex.web_boot();
const advance = (ms) => { let f = Math.round(ms * 44.1); while (f > 0) { const n = Math.min(1024, f); ex.web_render(n); f -= n; } };
advance(3000);

let held = new Map(), stateBuf = null, stateGot = 0, live = false;
const st = { inputs: 0, midi: 0, states: 0, sigs: 0, sigOk: 0, lastDev: 0, lastView: 0 };
function frame(m) {
  const n = m[3] | m[4] << 8 | m[5] << 16, cmd = m[2], b = m.slice(6, 6 + n);
  const u32 = (o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
  if (cmd === MR_INPUT && live) {
    st.inputs++;
    ex.web_buttons(b[0] | b[1] << 8);
    for (let i = 0; i < 7; i++) { const d = (b[6 + i] << 24) >> 24; if (d) ex.web_enc(i, d); }
    ex.web_master(b[13] | b[14] << 8);
    const now = new Map();
    for (let i = 0; n > MR_NIN && i < b[MR_NIN]; i++) now.set(b[MR_NIN + 1 + 3 * i], [b[MR_NIN + 2 + 3 * i], b[MR_NIN + 3 + 3 * i]]);
    for (const k of held.keys()) if (!now.has(k)) ex.web_key(k, 0, 0);
    for (const [k, [v, p]] of now) { const w = held.get(k); if (!w || w[1] !== p) ex.web_key(k, Math.max(1, v), Math.round(p * 1000 / 127)); }
    held = now;
  } else if (cmd === MR_MIDI) {
    for (let o = 0; o + 4 <= n; o += 4) { ex.web_midi_in(0, u32(o)); st.midi++; }
  } else if (cmd === MR_STATE) {
    const at = b[0] | b[1] << 8, total = b[2] | b[3] << 8;
    if (at === 0) { stateBuf = new Uint8Array(total); stateGot = 0; }
    stateBuf.set(b.slice(4), at); stateGot += n - 4;
    if (stateGot >= total) {
      new Uint8Array(mem.buffer, ex.web_state_buf(), total).set(stateBuf);
      ex.web_state_set(total); live = true; st.states++;
    }
  } else if (cmd === MR_SIG && live) {
    st.sigs++; st.lastDev = u32(0);
  }
}

// the FM-1
const dev = spawn(hostBin, ["--serve", fs.mkdtempSync("/tmp/live-")], { stdio: ["pipe", "pipe", "inherit"] });
const lines = readline.createInterface({ input: dev.stdout });
const waiting = [];
lines.on("line", (l) => {
  if (l.startsWith("sx ")) {
    const raw = Buffer.from(l.slice(3), "hex"), m = unpack7(raw.subarray(1, raw.length - 1));
    if (m[0] === 0 && m[1] === 0x59) frame(m);
  } else if (/^(ok|fail|  ok|FAIL)/.test(l)) waiting.shift()?.(l);
});
const cmd = (c) => new Promise((r) => { waiting.push(r); dev.stdin.write(c + "\n"); });
// both sides on: the FM-1 runs ms, then the viewer the same (its events arrive as the run's output)
async function run(ms) {
  for (let t = 0; t < ms; t += 16) { await cmd("run 16"); advance(16); }
}
let fails = 0;
async function check(what) {
  await run(600);                                     // (a signature comes every 500 ms)
  const v = ex.web_state_sig() >>> 0, ok = live && st.lastDev === v;
  console.log(`${ok ? "ok  " : "FAIL"} ${what}: FM-1 ${st.lastDev.toString(16)} viewer ${v.toString(16)}`);
  fails += !ok;
}

await run(1500);                                      // the FM-1 past its splash
dev.stdin.write("sx " + request(3) + "\n");      // (no answer line: its frames come with the next run)
await check("the state taken");
const B = (name) => ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"].indexOf(name);   // (plat.h B_*)
async function tap(btn) { await cmd(`buttons ${1 << btn}`); await run(64); await cmd("buttons 0"); await run(64); }
await tap(B("FX")); await check("FX page");
await tap(B("FX")); await check("FX's next page");
await cmd("turn 3 5"); await check("a knob turned");                  // (EN_K1)
await cmd("key 7 90"); await run(200); await cmd("key 9 60,30"); await check("two keys held, one leaning");
await cmd("key 7 0"); await cmd("key 9 0"); await check("keys let go");
await cmd("midi 643C9009"); await run(300); await cmd("midi 003C8008"); await check("a MIDI note");
await tap(B("HOME")); await cmd("turn 2 1"); await check("the next sound");   // (EN_PRESET)
await cmd("key 7 90"); await run(400);                 // the screens side by side: the same but for the sky's animation
const dump = fs.mkdtempSync("/tmp/live-") + "/fb.raw";
await cmd(`fbdump ${dump}`);
const a = new Uint16Array(fs.readFileSync(dump).buffer.slice(0)), v = new Uint16Array(mem.buffer, ex.web_fb(), 240 * 240);
let same = 0;
for (let i = 0; i < a.length; i++) same += a[i] === v[i];
let y0 = 240, y1 = -1;
for (let i = 0; i < a.length; i++) if (a[i] !== v[i]) { y0 = Math.min(y0, i / 240 | 0); y1 = Math.max(y1, i / 240 | 0); }
console.log(`screens: ${(100 * same / a.length).toFixed(1)}% of the pixels the same (rows ${y0}..${y1} differ)`);
console.log(`inputs ${st.inputs} midi ${st.midi} states ${st.states} sigs ${st.sigs}`);
dev.stdin.write("quit\n");
process.exit(fails ? 1 : 0);
