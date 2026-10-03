#!/usr/bin/env node
// atlaspack.mjs -- the atlaspack command line on Node, running the WebAssembly build (atlas_web.wasm).
//
//   node atlaspack.mjs <same arguments as atlaspack>
//   node atlaspack.mjs pack assets/media/sprites --out Build/atlas-out --format atlas,tp-json
//
// The real file system is mounted with NODEFS: on Windows the drive of the current folder is
// mounted at /<drive letter> (D:\ -> /d), on other systems / is mounted at /host, and the process
// changes into the current folder there, so relative paths work unchanged. Absolute paths in the
// arguments are translated (D:\a\b -> /d/a/b); other drives are mounted on demand.
// Limitations: paths printed by the tool are the mounted ones (/d/...); a project saved with
// --save-project whose folders are on another drive than the project file gets relative paths
// through the mount (/d/.. -> /e/...), which the native tool would not understand; slower than the
// native atlaspack (single thread, NODEFS file access); needs Node 18+.
import { fileURLToPath, pathToFileURL } from 'node:url';
import { dirname, join } from 'node:path';
import process from 'node:process';

const here = dirname(fileURLToPath(import.meta.url));
const { default: createAtlasModule } = await import(pathToFileURL(join(here, 'atlas_web.js')).href);

const M = await createAtlasModule({
  print: (s) => process.stdout.write(s + '\n'),
  printErr: (s) => process.stderr.write(s + '\n'),
});
const { FS } = M;
const NODEFS = M.NODEFS || FS.filesystems?.NODEFS;
if (!NODEFS) { console.error('atlaspack.mjs: this atlas_web.js was built without NODEFS (-lnodefs.js)'); process.exit(2); }

const win = process.platform === 'win32';
const mounted = new Map();   // host root -> mount point

function mount(hostRoot, point) {
  if (mounted.has(hostRoot)) return mounted.get(hostRoot);
  try { FS.mkdir(point); } catch { /* exists */ }
  FS.mount(NODEFS, { root: hostRoot }, point);
  mounted.set(hostRoot, point);
  return point;
}

// Host path (absolute) -> path inside the module's file system.
function toVirtual(hostPath) {
  if (win) {
    const m = /^([A-Za-z]):[\\/]?(.*)$/.exec(hostPath);
    if (!m) return hostPath;
    const letter = m[1].toLowerCase();
    const point = mount(`${letter.toUpperCase()}:\\`, '/' + letter);
    return (point + '/' + m[2].replace(/\\/g, '/')).replace(/\/+$/, '') || '/';
  }
  const point = mount('/', '/host');
  return point + hostPath;
}

function isHostAbsolute(p) { return win ? /^[A-Za-z]:[\\/]/.test(p) : p.startsWith('/'); }

function translateArg(a) {
  if (a.startsWith('--') && a.includes('=')) {
    const eq = a.indexOf('=');
    return a.slice(0, eq + 1) + translateArg(a.slice(eq + 1));
  }
  return isHostAbsolute(a) ? toVirtual(a) : (win ? a.replace(/\\/g, '/') : a);
}

FS.chdir(toVirtual(process.cwd()));
const args = process.argv.slice(2).map(translateArg);
const code = M.cliMain(args);
process.exitCode = code;
