// web_smoke_test.mjs -- drives headless Chrome/Edge over the DevTools protocol to test the web build:
// title screen -> Enter (new game) -> Esc + Enter (in-game menu: Save) -> page reload -> the save is back.
// usage (emsdk's node works; Node 22+ for the built-in WebSocket):
//   node tools/web_smoke_test.mjs http://127.0.0.1:8099/toms_game.html <folder for screenshots> [width,height]
// Start tools\serve_web.cmd first. Prints the game state after each step and the page console.
import { spawn } from 'node:child_process';
import { writeFileSync, mkdirSync } from 'node:fs';

const url = process.argv[2], outDir = process.argv[3];
import { existsSync } from 'node:fs';
const chrome = ['C:/Program Files/Google/Chrome/Application/chrome.exe', 'C:/Program Files (x86)/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', '/usr/bin/google-chrome', '/usr/bin/chromium'].find(p => existsSync(p));
if (!chrome) { console.log('ERROR no Chrome or Edge found'); process.exit(2); }
const port = 9333;
mkdirSync(outDir + '/prof', { recursive: true });
const proc = spawn(chrome, ['--headless=new', '--use-angle=swiftshader', '--enable-unsafe-swiftshader',
  '--window-size=' + (process.argv[4] || '1280,720'), `--remote-debugging-port=${port}`, `--user-data-dir=${outDir}/prof`, 'about:blank']);
const sleep = ms => new Promise(r => setTimeout(r, ms));

let ws, id = 0; const pending = new Map(); const logs = [];
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
  const s = await evalJs(`JSON.stringify({frames: Module.ccall('jsFrameCount','number',[],[]), title: Module.ccall('jsTitleOpen','number',[],[]), saves: (function(){try{return FS.readdir('/save').filter(f=>f!=='.'&&f!=='..')}catch(e){return String(e)}})()})`);
  console.log(tag, s);
}
async function waitReady() {
  for (let i = 0; i < 480; i++) { const ok = await evalJs(`window.tomsReady===true && Module.ccall('jsFrameCount','number',[],[])>10`); if (ok) return; await sleep(250); }
  console.log('diag', await evalJs(`JSON.stringify({calledRun: typeof Module!=='undefined' && Module.calledRun, frames: (function(){try{return Module.ccall('jsFrameCount','number',[],[])}catch(e){return String(e)}})(), raf: typeof requestAnimationFrame, hidden: document.hidden, vis: document.visibilityState})`)); throw new Error('game never started');
}

ws = new WebSocket(await connect());
await new Promise(r => ws.onopen = r);
ws.onmessage = ev => { const m = JSON.parse(ev.data); if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
  else if (m.method === 'Runtime.consoleAPICalled') logs.push(m.params.args.map(a => a.value ?? a.description).join(' ')); };
await send('Runtime.enable'); await send('Page.enable');
try {
  await send('Page.navigate', { url });
  await waitReady(); await state('loaded:'); await shot('01_title');
  await send('Runtime.evaluate', { expression: "document.getElementById('canvas').focus()" });
  await key('Enter', 'Enter', 13); await sleep(1500); await state('after Enter:'); await shot('02_stage');
  // Move right a few tiles, then open the in-game menu and pick the first entry (Save).
  for (let i = 0; i < 3; i++) await key('ArrowRight', 'ArrowRight', 39);
  await key('Escape', 'Escape', 27); await sleep(500); await shot('03_menu');
  await key('Enter', 'Enter', 13); await sleep(1500); await state('after Save:'); await shot('04_after_save');
  // Reload: IndexedDB should bring the save back.
  await send('Page.reload', {}); await sleep(1000); await waitReady(); await sleep(2000);
  await state('after reload:'); await shot('05_reloaded_title');
} catch (e) { console.log('ERROR', e.message); process.exitCode = 1; }
console.log('--- page console (filtered) ---');
for (const l of logs) if (!/getInternalformatParameter/.test(l)) console.log(l);
ws.close(); proc.kill();
process.exit(process.exitCode || 0);
