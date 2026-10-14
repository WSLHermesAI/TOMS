// sync_floor_index.mjs -- rebuilds assets/data/events/floor_index.json from the command line.
//
//   node tools/web_editors/sync_floor_index.mjs          rebuild it (writes only when it changed)
//   node tools/web_editors/sync_floor_index.mjs --check  exit 1 when it is out of date, write nothing
//
// The HTML editors rebuild the index whenever they open or save the project folder (floor_index.js). After
// editing floors, pools or chapters by hand, run this instead: it runs the same floor_index.js, on the folder
// through a small stand-in for the browser's File System Access handles, so both write the same file.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '..', '..');

// FileSystemDirectoryHandle / FileSystemFileHandle, as much as floor_index.js uses
function fileHandle(p) {
  return {
    kind: 'file',
    async getFile() { return { text: async () => fs.readFileSync(p, 'utf8') }; },
    async createWritable() {
      let out = '';
      return { write: async s => { out += s; }, close: async () => fs.writeFileSync(p, out) };
    },
  };
}
function dirHandle(p) {
  return {
    kind: 'directory',
    async getDirectoryHandle(name, opt) {
      const q = path.join(p, name);
      if (!fs.existsSync(q)) { if (opt && opt.create) fs.mkdirSync(q); else throw new Error('NotFound ' + q); }
      return dirHandle(q);
    },
    async getFileHandle(name, opt) {
      const q = path.join(p, name);
      if (!fs.existsSync(q) && !(opt && opt.create)) throw new Error('NotFound ' + q);
      return fileHandle(q);
    },
    async *entries() {
      for (const n of fs.readdirSync(p)) {
        const q = path.join(p, n);
        yield [n, fs.statSync(q).isDirectory() ? dirHandle(q) : fileHandle(q)];
      }
    },
  };
}

const window = {};
vm.runInNewContext(fs.readFileSync(path.join(here, 'floor_index.js'), 'utf8'), { window, JSON, Object, Array, Map, Set, Promise, console });
const dir = dirHandle(root);
const file = path.join(root, 'assets', 'data', 'events', 'floor_index.json');
if (process.argv.includes('--check')) {
  const want = JSON.stringify(await window.buildFloorIndex(dir), null, 1) + '\n';
  const same = fs.existsSync(file) && fs.readFileSync(file, 'utf8') === want;
  console.log(same ? 'floor_index.json is up to date' : 'floor_index.json is out of date: run node tools/web_editors/sync_floor_index.mjs');
  process.exit(same ? 0 : 1);
}
const wrote = await window.syncFloorIndex(dir);
console.log(wrote ? 'wrote assets/data/events/floor_index.json' : 'floor_index.json was already up to date');
