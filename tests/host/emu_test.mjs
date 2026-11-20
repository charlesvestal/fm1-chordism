// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// The browser build in Node: boot it, hold a key (a chord sounds), and expect sound and screen frames.
import fs from "fs";
const wasm = fs.readFileSync(process.argv[2]);
let mem;
const wasi = new Proxy({ clock_time_get: (id, p, out) => { new DataView(mem.buffer).setBigUint64(out, 0n, true); return 0; },
  fd_write: (fd, iov, n, out) => { new DataView(mem.buffer).setUint32(out, 0, true); return 0; } }, { get: (t, k) => k in t ? t[k] : () => 0 });
const { instance } = await WebAssembly.instantiate(wasm, { wasi_snapshot_preview1: wasi, env: new Proxy({}, { get: () => () => 0 }) });
const ex = instance.exports; mem = ex.memory;
if (ex._initialize) ex._initialize();
ex.web_master(4096); ex.web_boot();
function run(blocks) {
  let peak = 0;
  for (let i = 0; i < blocks; i++) {
    ex.web_render(128);
    const l = new Float32Array(mem.buffer, ex.web_out_l(), 128);
    for (const v of l) peak = Math.max(peak, Math.abs(v));
  }
  return peak;
}
const quiet = run(700);
ex.web_keys(1 << 7); const chord = run(400); ex.web_keys(0);
console.log("quiet peak", quiet.toFixed(3), "chord peak", chord.toFixed(3), "blits", ex.web_blits());
if (quiet > 0.01 || chord < 0.05 || ex.web_blits() < 10) process.exit(1);
