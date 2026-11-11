// web_smoke_test.mjs -- drives headless Chrome/Edge over the DevTools protocol to test the web build:
// title screen -> Enter (new game) -> Esc + Enter (in-game menu: Save) -> page reload -> the save is back.
//
//   node tools/web_smoke_test.mjs <web build folder | URL> <folder for screenshots> [width,height]
//        [--isolate] [--expect-threads=yes|no] [--query=a=b] [--gpu]
//
//   --isolate            the built-in server sends COOP/COEP (cross-origin isolation), which the
//                        multithreaded build needs (docs/engine/02_THREADS.md)
//   --expect-threads=    yes: the page must run with job-system workers; no: without
//   --query=             appended to the page URL (e.g. nothreads, the page's opt-out)
//   --battle             after New Game, try a weapon (Down, Enter): the tutorial's fight on floor 1's 3D stage (docs/gameplay/21);
//                        checks the stage loads (the console's [battle3d] stage line) and stops there (no save, no reload)
//   --gpu                the real GPU (Windows: ANGLE on Direct3D 11, what Chrome uses there) instead of
//                        the SwiftShader software GPU. The real path is stricter: a shader that links on
//                        SwiftShader can fail there (2026-10-05: fs_mesh with shadow lookups).
// Every WebGL shader that fails to compile or link is logged ([webgl] ...) and fails the run; so does a program over the limits
// OpenGL ES 3.0 guarantees (it links on this PC and fails on a phone).
//
// Give it a folder (Build\web-release-windows\bin, or Build\dist\TOMS-web) and it serves that folder
// itself; or give it the URL of a running server (tools\serve_web.cmd). Needs Node 22+ (built-in
// WebSocket; emsdk's node is older) and Chrome or Edge. Each run starts with an empty browser
// profile, so no save from an earlier run is there.
// Exit code: 0 pass, 1 fail, 77 skipped (no Chrome/Edge, Node too old, no web build) -- CTest's
// "web" test (tests/CMakeLists.txt) reads 77 as "skipped".
import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdirSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { extname, join, resolve } from 'node:path';

const SKIP = 77;
const flags = process.argv.slice(2).filter(a => a.startsWith('--'));
const [target, outDir, windowSize] = process.argv.slice(2).filter(a => !a.startsWith('--'));
const flag = name => { const f = flags.find(a => a === '--' + name || a.startsWith('--' + name + '=')); return f === undefined ? undefined : (f.split('=').slice(1).join('=') || true); };
const isolate = !!flag('isolate'), expectThreads = flag('expect-threads'), query = flag('query'), gpu = !!flag('gpu'), battle = !!flag('battle');
if (!target || !outDir) { console.log('usage: node web_smoke_test.mjs <web build folder | URL> <screenshot folder> [w,h]'); process.exit(2); }
if (typeof WebSocket === 'undefined') { console.log(`SKIP: Node ${process.version} has no built-in WebSocket (needs Node 22+)`); process.exit(SKIP); }
const chrome = ['C:/Program Files/Google/Chrome/Application/chrome.exe', 'C:/Program Files (x86)/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', '/usr/bin/google-chrome', '/usr/bin/chromium'].find(p => existsSync(p));
if (!chrome) { console.log('SKIP: no Chrome or Edge found'); process.exit(SKIP); }

// ---- serve a build folder ourselves (or use the URL as given) ----
let url = target, server = null;
if (!/^https?:/.test(target)) {
  const root = resolve(target);
  if (!existsSync(root) || !statSync(root).isDirectory()) { console.log(`SKIP: no web build at ${root}`); process.exit(SKIP); }
  const page = ['toms_game.html', 'index.html'].find(f => existsSync(join(root, f)));
  if (!page) { console.log(`SKIP: ${root} has no toms_game.html / index.html`); process.exit(SKIP); }
  const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.data': 'application/octet-stream',
                  '.png': 'image/png', '.json': 'application/json', '.css': 'text/css' };
  server = createServer((req, res) => {
    const file = join(root, decodeURIComponent(req.url.split('?')[0]));
    if (!file.startsWith(root) || !existsSync(file) || statSync(file).isDirectory()) { res.writeHead(404); res.end(); return; }
    const headers = { 'Content-Type': types[extname(file)] || 'application/octet-stream' };
    if (isolate) { headers['Cross-Origin-Opener-Policy'] = 'same-origin'; headers['Cross-Origin-Embedder-Policy'] = 'require-corp'; }
    res.writeHead(200, headers);
    res.end(readFileSync(file));
  });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  url = `http://127.0.0.1:${server.address().port}/${page}`;
}
if (query) url += (url.includes('?') ? '&' : '?') + query;
console.log('testing', url);

rmSync(outDir + '/prof', { recursive: true, force: true });   // a fresh profile: no saves from earlier runs
mkdirSync(outDir + '/prof', { recursive: true });
const port = 9333;
const gpuFlags = gpu ? [process.platform === 'win32' ? '--use-angle=d3d11' : '--use-angle=default', '--enable-gpu', '--ignore-gpu-blocklist']
                    : ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'];
const proc = spawn(chrome, ['--headless=new', ...gpuFlags,
  '--window-size=' + (windowSize || '1280,720'), `--remote-debugging-port=${port}`, `--user-data-dir=${outDir}/prof`, 'about:blank']);
const sleep = ms => new Promise(r => setTimeout(r, ms));

let ws, id = 0; const pending = new Map(); const logs = []; const errors = []; const failures = [];
function check(ok, what) { console.log(ok ? '  ok  ' : '  FAIL', what); if (!ok) failures.push(what); }
async function connect() {
  for (let i = 0; i < 50; i++) {
    try { const r = await fetch(`http://127.0.0.1:${port}/json`); const t = (await r.json()).find(x => x.type === 'page'); if (t) return t.webSocketDebuggerUrl; } catch {}
    await sleep(200);
  }
  throw new Error('no chrome');
}
function send(method, params = {}) {
  return new Promise(res => { const i = ++id; pending.set(i, res); ws.send(JSON.stringify({ id: i, method, params })); });
}
async function evalJs(expr) { const r = await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true }); return r.result?.result?.value; }
async function shot(name) { const r = await send('Page.captureScreenshot', { format: 'png' }); writeFileSync(`${outDir}/${name}.png`, Buffer.from(r.result.data, 'base64')); console.log('shot', name); }
async function key(k, code, keyCode) {
  await send('Input.dispatchKeyEvent', { type: 'keyDown', key: k, code, windowsVirtualKeyCode: keyCode, nativeVirtualKeyCode: keyCode });
  await sleep(150);
  await send('Input.dispatchKeyEvent', { type: 'keyUp', key: k, code, windowsVirtualKeyCode: keyCode, nativeVirtualKeyCode: keyCode });
  await sleep(400);
}
async function state(tag) {
  const s = JSON.parse(await evalJs(`JSON.stringify({frames: Module.ccall('jsFrameCount','number',[],[]), title: Module.ccall('jsTitleOpen','number',[],[]), saves: (function(){try{return FS.readdir('/save').filter(f=>f!=='.'&&f!=='..')}catch(e){return []}})()})`));
  console.log(tag, JSON.stringify(s));
  return s;
}
async function waitReady() {
  for (let i = 0; i < 480; i++) { const ok = await evalJs(`window.tomsReady===true && Module.ccall('jsFrameCount','number',[],[])>10`); if (ok) return; await sleep(250); }
  console.log('diag', await evalJs(`JSON.stringify({calledRun: typeof Module!=='undefined' && Module.calledRun, frames: (function(){try{return Module.ccall('jsFrameCount','number',[],[])}catch(e){return String(e)}})(), raf: typeof requestAnimationFrame, hidden: document.hidden, vis: document.visibilityState})`)); throw new Error('game never started');
}

ws = new WebSocket(await connect());
await new Promise(r => ws.onopen = r);
ws.onmessage = ev => { const m = JSON.parse(ev.data); if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
  else if (m.method === 'Runtime.consoleAPICalled') logs.push(m.params.args.map(a => a.value ?? a.description).join(' '));
  else if (m.method === 'Runtime.exceptionThrown') errors.push(m.params.exceptionDetails?.exception?.description || m.params.exceptionDetails?.text); };
await send('Runtime.enable'); await send('Page.enable');
// Report every WebGL shader that fails to compile or link (bgfx itself only says "Failed to compile shader").
await send('Page.addScriptToEvaluateOnNewDocument', { source: `(() => {
  const P = WebGL2RenderingContext.prototype, cs = P.compileShader, lp = P.linkProgram, ss = P.shaderSource, as = P.attachShader;
  // The limits OpenGL ES 3.0 guarantees. A desktop GPU allows far more, so a shader over them links here and fails on a phone or
  // tablet, silently: nothing is drawn (2026-11-04: the glTF title on a Xiaomi Pad, u_joints[128] = 512 vectors).
  const VS_VEC = 256, FS_VEC = 224, SAMPLERS = 16, sources = new Map();
  const VEC = { 0x8B5C: 4, 0x8B5B: 3, 0x8B5A: 2 }, SAMPLER = [0x8B5E, 0x8B5F, 0x8B60, 0x8B62, 0x8DC1, 0x8DC5, 0x8B62];
  P.shaderSource = function (sh, text) { sources.set(sh, text); return ss.call(this, sh, text); };
  P.attachShader = function (pr, sh) { (pr.tomsShaders = pr.tomsShaders || []).push(sh); return as.call(this, pr, sh); };
  function checkLimits(gl, pr) {
    const texts = (pr.tomsShaders || []).map(sh => sources.get(sh) || ''), vs = texts.find(t => /gl_Position/.test(t)) || '', fs = texts.find(t => !/gl_Position/.test(t)) || '';
    let vsVec = 0, fsVec = 0, vsTex = 0, fsTex = 0;
    for (let i = 0, n = gl.getProgramParameter(pr, gl.ACTIVE_UNIFORMS); i < n; i++) {
      const u = gl.getActiveUniform(pr, i), name = u.name.split('[')[0], isSampler = SAMPLER.includes(u.type), count = (VEC[u.type] || 1) * u.size;
      if (vs.includes(name)) { if (isSampler) vsTex += u.size; else vsVec += count; }
      if (fs.includes(name)) { if (isSampler) fsTex += u.size; else fsVec += count; }
    }
    if (vsVec > VS_VEC || fsVec > FS_VEC || vsTex > SAMPLERS || fsTex > SAMPLERS)
      console.log('[webgl] program over the ES 3.0 limits: vertex ' + vsVec + '/' + VS_VEC + ' vectors, fragment ' + fsVec + '/' + FS_VEC + ', samplers ' + vsTex + ' and ' + fsTex + '/' + SAMPLERS);
    (window.tomsBudget = window.tomsBudget || []).push([vsVec, fsVec, vsTex, fsTex]);
  }
  P.compileShader = function (sh) { cs.call(this, sh); if (!this.getShaderParameter(sh, this.COMPILE_STATUS))
    console.log('[webgl] shader compile failed: ' + JSON.stringify(this.getShaderInfoLog(sh)) + ' ' + JSON.stringify((this.getShaderSource(sh) || '').slice(0, 200))); };
  P.linkProgram = function (p) { lp.call(this, p); if (!this.getProgramParameter(p, this.LINK_STATUS)) {
    console.log('[webgl] program link failed: ' + JSON.stringify(this.getProgramInfoLog(p))); return; }
    checkLimits(this, p); };
})();` });
try {
  await send('Page.navigate', { url });
  await waitReady();
  let s = await state('loaded:'); await shot('01_title');
  check(s.title === 1, 'the title screen is up after loading');
  const t = JSON.parse(await evalJs(`JSON.stringify({isolated: self.crossOriginIsolated === true, threadedPage: window.tomsThreaded === true, workers: Module.ccall('jsWorkerCount','number',[],[])})`));
  console.log('threads:', JSON.stringify(t));
  if (expectThreads === 'yes') check(t.isolated && t.workers > 0, `the page runs threaded (isolated=${t.isolated}, workers=${t.workers})`);
  if (expectThreads === 'no') check(t.workers === 0, `the page runs single-threaded (workers=${t.workers})`);
  await send('Runtime.evaluate', { expression: "document.getElementById('canvas').focus()" });
  await key('Enter', 'Enter', 13); await sleep(1500);
  s = await state('after Enter:'); await shot('02_stage');
  check(s.title === 0, 'Enter starts a new game (the title closes)');
  if (battle) {   // the weapon master's question: try the bow -> the tutorial's fight, on the 3D stage
    await key('ArrowDown', 'ArrowDown', 40); await key('Enter', 'Enter', 13);
    let stage = '';
    for (let i = 0; i < 60 && !stage; i++) { await sleep(500); stage = logs.find(l => /\[battle3d\].*stage:/.test(l)) || ''; }
    await sleep(2500); await shot('02b_battle');
    check(!!stage, 'the 3D battle stage loads in the browser' + (stage ? ': ' + stage : ''));
    check(!logs.some(l => /\[battle3d\] (no 3D stage|too slow)/.test(l)), 'the stage is drawn (not the painted backdrop)');
    throw { battleDone: true };
  }
  // Move right a few tiles, then open the in-game menu and pick the first entry (Save).
  for (let i = 0; i < 3; i++) await key('ArrowRight', 'ArrowRight', 39);
  await key('Escape', 'Escape', 27); await sleep(500); await shot('03_menu');
  await key('Enter', 'Enter', 13); await sleep(1500);
  s = await state('after Save:'); await shot('04_after_save');
  const saved = s.saves.filter(f => /^slot\d+\.json$/.test(f));
  check(saved.length > 0, 'the menu Save writes a slot file');
  // Reload: IndexedDB should bring the save back.
  await send('Page.reload', {}); await sleep(1000); await waitReady(); await sleep(2000);
  s = await state('after reload:'); await shot('05_reloaded_title');
  check(s.title === 1, 'after a reload the title screen is up again');
  check(saved.every(f => s.saves.includes(f)), 'the save survives the reload (IndexedDB)');
  check(s.frames > 10, 'the game keeps drawing frames after the reload');
} catch (e) { if (!e.battleDone) check(false, 'the run finished: ' + e.message); }
check(errors.length === 0, 'no uncaught page errors' + (errors.length ? ': ' + errors.join(' | ') : ''));
check(!logs.some(l => l.startsWith('[webgl]')), 'every WebGL shader compiles and links');
console.log('--- page console (filtered) ---');
for (const l of logs) if (!/getInternalformatParameter/.test(l)) console.log(l);
console.log(failures.length ? `FAILED (${failures.length})` : 'PASSED');
ws.close(); proc.kill(); if (server) server.close();
process.exit(failures.length ? 1 : 0);
