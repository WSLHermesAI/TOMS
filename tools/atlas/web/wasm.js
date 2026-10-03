// wasm.js -- loads atlas_web.wasm and keeps the in-memory workspace (Emscripten MEMFS).
//
// The project (with its images) lives in the C++ side (atlas_web.cpp); MEMFS holds the files the
// page hands over: an opened project with its packed atlas under /work/<relative path>, reference
// folders at the place the project expects them, and the folders Save / Extract write before they
// are zipped.

export async function loadWorkspace(log = () => {}) {
  const { default: createAtlasModule } = await import('./atlas_web.js');
  const M = await createAtlasModule({
    print: (s) => log(s, 'info'),
    printErr: (s) => log(s, 'error'),
  });
  return new Workspace(M);
}

export class Workspace {
  constructor(M) {
    this.M = M;
    this.A = new M.AtlasWeb();
    this.roots = new Set();          // top-level MEMFS folders this session wrote (cleared on reset)
    this.formats = JSON.parse(this.A.formats());
  }

  write(path, bytes) {
    const top = '/' + path.split('/').filter(Boolean)[0];
    this.roots.add(top);
    if (!this.A.writeFile(path, bytes)) throw new Error('cannot write ' + path);
  }
  read(path) { return this.A.readFile(path); }
  remove(path) { return this.A.removeFile(path); }
  exists(path) { return this.A.exists(path); }
  list(dir) { return JSON.parse(this.A.listFiles(dir)); }
  listAll() { return [...this.roots].flatMap((r) => this.list(r)); }
  reset() {
    for (const r of this.roots) this.A.removeFile(r);
    this.roots.clear();
  }

  // The page edits the project as JSON; the images stay in C++ (setProject keeps them).
  setProject(project, path) {
    const err = this.A.setProject(JSON.stringify(project), path);
    if (err) throw new Error(err);
  }
  // The project as the core writes it (normalized, every field present).
  // Floats come back as float32 -> double (0.15 -> 0.15000000596...): rounded to 6 decimals.
  projectJson() {
    return JSON.parse(this.A.getProject(), (k, v) => (typeof v === 'number' && !Number.isInteger(v) ? Math.round(v * 1e6) / 1e6 : v));
  }
  newProject(name, path) { this.A.newProject(name, path); return this.projectJson(); }
  inspect(path) { return JSON.parse(this.A.inspect(path)); }
  openProject(path, dropMissingVariants = false) { return JSON.parse(this.A.openProject(path, dropMissingVariants)); }
  resolvePath(rel) { return this.A.resolvePath(rel); }
  relativize(abs) { return this.A.relativize(abs); }
  resolveSprites() { return JSON.parse(this.A.resolve()); }
  build(variant = '') { return JSON.parse(this.A.build(variant)); }
  pageRGBA(i) { return this.A.getPageRGBA(i); }
  pagePng(i) { return this.A.getPagePng(i); }
  regionRGBA(name) { return this.A.getRegionRGBA(name); }
  regionPng(name) { return this.A.getRegionPng(name); }
  exportFiles(formats) { return this.A.exportFiles(formats.join(',')); }
  importAtlas(path, projectPath) { return JSON.parse(this.A.importAtlas(path, projectPath)); }
  // images (base art, or a variant's replacement art); setters return '' or the error
  setImageBytes(name, bytes, variant = '') { return this.A.setImageBytes(name, bytes, variant); }
  setImageFile(name, path, variant = '') { return this.A.setImageFile(name, path, variant); }
  hasImage(name, variant = '') { return this.A.hasImage(name, variant); }
  imageNames(variant = '') { return JSON.parse(this.A.imageNames(variant)); }
  imageNameFor(file, folder = '', prefix = '') { return this.A.imageNameFor(file, folder, prefix); }
  renameSprite(from, to) { return this.A.renameSprite(from, to); }
  deleteSprites(names) { return this.A.deleteSprites(JSON.stringify(names)); }
  scanReferences(variant = '') { return JSON.parse(this.A.scanReferences(variant)); }
  // undo snapshots (copies of the C++ project, images shared)
  snapshot() { return this.A.snapshot(); }
  restore(id) { return this.A.restore(id); }
  keepSnapshots(ids) { return this.A.keepSnapshots(JSON.stringify([...ids])); }
  outputDirs(path) { return JSON.parse(this.A.outputDirs(path)); }
  saveAll(path, also = []) { return JSON.parse(this.A.saveAll(path, also.join(','))); }
  extract(dir, variant = '', children = false) { return JSON.parse(this.A.extract(dir, variant, children)); }
  cli(args) { return this.M.cliMain(args); }
}

// ---- dropped / picked files -> [{rel: 'folder/sub/a.png', file: File}] ----

// From a drop: folders are walked (DataTransferItem.webkitGetAsEntry). The entries must be taken
// synchronously inside the drop handler: call this before any await.
export function collectDrop(dataTransfer) {
  const entries = [];
  const loose = [];
  for (const item of dataTransfer.items || []) {
    if (item.kind !== 'file') continue;
    const entry = item.webkitGetAsEntry ? item.webkitGetAsEntry() : null;
    if (entry) entries.push(entry);
    else { const f = item.getAsFile(); if (f) loose.push(f); }
  }
  if (!entries.length && !loose.length) for (const f of dataTransfer.files || []) loose.push(f);
  return (async () => {
    const out = loose.map((file) => ({ rel: file.name, file }));
    const walk = async (entry, prefix) => {
      if (entry.isFile) {
        const file = await new Promise((res, rej) => entry.file(res, rej));
        out.push({ rel: prefix + entry.name, file });
      } else if (entry.isDirectory) {
        const reader = entry.createReader();
        for (;;) {   // readEntries returns the children in batches until an empty one
          const batch = await new Promise((res, rej) => reader.readEntries(res, rej));
          if (!batch.length) break;
          for (const e of batch) await walk(e, prefix + entry.name + '/');
        }
      }
    };
    for (const e of entries) await walk(e, '');
    return out;
  })();
}

// From <input type=file> (with or without webkitdirectory).
export function collectInput(fileList) {
  return [...fileList].map((file) => ({ rel: file.webkitRelativePath || file.name, file }));
}
