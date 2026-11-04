// ocean_web_test.mjs -- the ocean viewer's web build in headless Chrome or Edge, on the real GPU: it starts, no shader fails,
// and how fast it runs. A phone can be imitated: its screen, its user agent, and a slower CPU (not its GPU).
//
//   node ocean_web_test.mjs <web build folder> <folder for pictures> [--query=preset=storm] [--phone] [--cpu=<slower by>] [--seconds=<n>] [--software]
//   --software: Chrome's software GPU instead of the real one: very slow, a worst case that makes auto quality step down
//
// Exit code: 0 pass, 1 fail, 77 skipped (no Chrome/Edge, Node too old, no web build).
import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdirSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { extname, join, resolve } from 'node:path';

const SKIP = 77;
const args = process.argv.slice(2);
const flag = name => { const f = args.find(a => a === '--' + name || a.startsWith('--' + name + '=')); return f === undefined ? undefined : (f.split('=').slice(1).join('=') || true); };
const [target, outArg] = args.filter(a => !a.startsWith('--'));
if (!target || !outArg) { console.log('usage: node ocean_web_test.mjs <web build folder> <picture folder> [--query=..] [--phone] [--cpu=4] [--seconds=8]'); process.exit(2); }
if (typeof WebSocket === 'undefined') { console.log(`SKIP: Node ${process.version} has no built-in WebSocket (needs Node 22+)`); process.exit(SKIP); }
const chrome = ['C:/Program Files/Google/Chrome/Application/chrome.exe', 'C:/Program Files (x86)/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', '/usr/bin/google-chrome', '/usr/bin/chromium'].find(p => existsSync(p));
if (!chrome) { console.log('SKIP: no Chrome or Edge found'); process.exit(SKIP); }
const root = resolve(target), outDir = resolve(outArg);   // Chrome's profile folder must be a full path
if (!existsSync(join(root, 'ocean_viewer.html'))) { console.log(`SKIP: no ocean_viewer.html in ${root} (tools\\build_ocean_web.cmd)`); process.exit(SKIP); }

const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.data': 'application/octet-stream' };
const server = createServer((req, res) => {
  const file = join(root, decodeURIComponent(req.url.split('?')[0]));
  if (!file.startsWith(root) || !existsSync(file) || statSync(file).isDirectory()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': types[extname(file)] || 'application/octet-stream' });
  res.end(readFileSync(file));
});
await new Promise(r => server.listen(0, '127.0.0.1', r));
const query = flag('query');
const url = `http://127.0.0.1:${server.address().port}/ocean_viewer.html` + (query ? '?' + query : '');
const seconds = Number(flag('seconds') || 8), cpu = Number(flag('cpu') || 1), phone = !!flag('phone');
console.log('testing', url, phone ? '(as a phone)' : '', cpu > 1 ? `(CPU ${cpu}x slower)` : '');

rmSync(outDir + '/prof', { recursive: true, force: true });
mkdirSync(outDir + '/prof', { recursive: true });
const port = 9344;
const proc = spawn(chrome, ['--headless=new', ...(flag('software') ? ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'] : [process.platform === 'win32' ? '--use-angle=d3d11' : '--use-angle=default', '--enable-gpu', '--ignore-gpu-blocklist']),
  '--window-size=1280,720', `--remote-debugging-port=${port}`, `--user-data-dir=${outDir}/prof`, 'about:blank']);
const sleep = ms => new Promise(r => setTimeout(r, ms));
let ws, id = 0; const pending = new Map(), logs = [], errors = [];
function send(method, params = {}) { return new Promise(res => { const i = ++id; pending.set(i, res); ws.send(JSON.stringify({ id: i, method, params })); }); }
async function evalJs(e) { const r = await send('Runtime.evaluate', { expression: e, returnByValue: true }); return r.result?.result?.value; }
let code = 0;
try {
  let wsUrl = null;
  for (let i = 0; i < 50 && !wsUrl; i++) {
    try { const t = (await (await fetch(`http://127.0.0.1:${port}/json`)).json()).find(x => x.type === 'page'); if (t) wsUrl = t.webSocketDebuggerUrl; } catch {}
    if (!wsUrl) await sleep(200);
  }
  if (!wsUrl) throw new Error('no chrome');
  ws = new WebSocket(wsUrl);
  await new Promise(r => ws.onopen = r);
  ws.onmessage = ev => { const m = JSON.parse(ev.data); if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
    else if (m.method === 'Runtime.consoleAPICalled') logs.push(m.params.args.map(a => a.value ?? a.description).join(' '));
    else if (m.method === 'Runtime.exceptionThrown') errors.push(m.params.exceptionDetails?.exception?.description || m.params.exceptionDetails?.text); };
  await send('Runtime.enable'); await send('Page.enable');
  await send('Page.addScriptToEvaluateOnNewDocument', { source: `(() => {
    const P = WebGL2RenderingContext.prototype, cs = P.compileShader, lp = P.linkProgram;
    P.compileShader = function (sh) { cs.call(this, sh); if (!this.getShaderParameter(sh, this.COMPILE_STATUS)) console.log('[webgl] shader compile failed: ' + JSON.stringify(this.getShaderInfoLog(sh))); };
    P.linkProgram = function (p) { lp.call(this, p); if (!this.getProgramParameter(p, this.LINK_STATUS)) console.log('[webgl] program link failed: ' + JSON.stringify(this.getProgramInfoLog(p))); };
  })();` });
  if (phone) {
    await send('Emulation.setUserAgentOverride', { userAgent: 'Mozilla/5.0 (Linux; Android 13; Pixel 6) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Mobile Safari/537.36', userAgentMetadata: { mobile: true, platform: 'Android', platformVersion: '13', architecture: '', model: 'Pixel 6', brands: [] } });
    await send('Emulation.setDeviceMetricsOverride', { width: 412, height: 915, deviceScaleFactor: 2.625, mobile: true });
  }
  if (cpu > 1) await send('Emulation.setCPUThrottlingRate', { rate: cpu });
  await send('Page.navigate', { url });
  let started = false;
  for (let i = 0; i < 240 && !started; i++) { started = (await evalJs('window.tomsOcean ? window.tomsOcean.frames : 0')) > 5; if (!started) await sleep(250); }
  if (!started) throw new Error('it never drew a frame');
  const a = JSON.parse(await evalJs('JSON.stringify(window.tomsOcean)')), t0 = Date.now();
  await sleep(seconds * 1000);
  const b = JSON.parse(await evalJs('JSON.stringify(window.tomsOcean)'));
  const fps = (b.frames - a.frames) * 1000 / (Date.now() - t0);
  console.log(`${b.width} x ${b.height}, quality ${a.quality} -> ${b.quality}: ${fps.toFixed(1)} fps, waves ${b.simMs.toFixed(1)} ms, pictures ${b.uploadMs.toFixed(1)} ms`);
  const shot = await send('Page.captureScreenshot', { format: 'png' });
  writeFileSync(`${outDir}/ocean${phone ? '_phone' : ''}.png`, Buffer.from(shot.result.data, 'base64'));
  const bad = logs.filter(l => l.includes('[webgl]') || /FATAL|Failed to compile/.test(l)).concat(errors);
  logs.filter(l => l.includes('[ocean]')).forEach(l => console.log(l));
  if (bad.length) { console.log('FAIL:\n' + bad.join('\n')); code = 1; }
  if (b.frames <= a.frames) { console.log('FAIL: frames stopped'); code = 1; }
} catch (e) { console.log('FAIL:', e.message); logs.slice(-15).forEach(l => console.log(l)); code = 1; }
proc.kill(); server.close();
process.exit(code);
