// web_smoke_test.mjs -- drives headless Chrome/Edge over the DevTools protocol to test the web build:
// title screen -> Enter (new game) -> Esc + Enter (in-game menu: Save) -> page reload -> the save is back.
//
//   node tools/web_smoke_test.mjs <web build folder | URL> <folder for screenshots> [width,height]
//        [--isolate] [--expect-threads=yes|no] [--query=a=b]
//
//   --isolate            the built-in server sends COOP/COEP (cross-origin isolation), which the
//                        multithreaded build needs (docs/10_THREADS.md)
//   --expect-threads=    yes: the page must run with job-system workers; no: without
//   --query=             appended to the page URL (e.g. nothreads, the page's opt-out)
//
// Give it a folder (build-web-release-windows\bin, or dist\TOMS-web) and it serves that folder
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
const isolate = !!flag('isolate'), expectThreads = flag('expect-threads'), query = flag('query');
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
const proc = spawn(chrome, ['--headless=new', '--use-angle=swiftshader', '--enable-unsafe-swiftshader',
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
} catch (e) { check(false, 'the run finished: ' + e.message); }
check(errors.length === 0, 'no uncaught page errors' + (errors.length ? ': ' + errors.join(' | ') : ''));
console.log('--- page console (filtered) ---');
for (const l of logs) if (!/getInternalformatParameter/.test(l)) console.log(l);
console.log(failures.length ? `FAILED (${failures.length})` : 'PASSED');
ws.close(); proc.kill(); if (server) server.close();
process.exit(failures.length ? 1 : 0);
