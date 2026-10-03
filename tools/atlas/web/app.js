// app.js -- the atlas web editor: state, undo/redo, toolbar, file handling, rebuilds.
// Panels: sprites.js (left), canvas.js (center), props.js (right), anims.js (bottom);
// refs.js: the folder dialogs (Import from references, missing folders when opening).
//
// Every project is EMBEDDED (like the old PI editor): the images live in the C++ project
// (atlas_web.cpp) and in the packed atlas Save writes next to the project file. The page keeps the
// project's JSON for the panels; undo/redo steps are C++ snapshots, so they include the images.
import { loadWorkspace, collectDrop, collectInput } from './wasm.js';
import { makeZip, unzip } from './zip.js';
import { $, $$, el, debounce, prefs, setPref, download, baseName, dirName, stem, ext,
         confirmDialog, promptDialog, choiceDialog, pickFiles, contextMenu, toast, typingInField, ALWAYS_EXPORTED } from './util.js';
import { referencesDialog, folderSlotsDialog, itemBytes } from './refs.js';
import { SpritesPanel } from './sprites.js';
import { AtlasView } from './canvas.js';
import { PropsPanel } from './props.js';
import { AnimsPanel } from './anims.js';

const WORK = '/work';         // opened files, at their paths relative to what was picked
const SAVE = '/save';         // Save writes here, then it is zipped
const EXTRACT = '/extract';   // Extract images writes here, then it is zipped
const HISTORY = 200;          // undo steps kept

// ---- the store --------------------------------------------------------------------------------
class App extends EventTarget {
  constructor(ws) {
    super();
    this.ws = ws;
    this.project = null;
    this.projectPath = WORK + '/atlas.atlasproj';
    this.fileName = 'atlas.atlasproj';
    this.variant = '';
    this.result = null;           // last build (parsed JSON from atlas_web.cpp)
    this.regions = new Map();     // name -> region
    this.resolved = [];           // sprites as the build sees them
    this.resolvedMap = new Map();
    this.pages = [];              // canvases with the page pixels
    this.page = 0;
    this.selection = [];          // sprite names, the last one is the "current" one
    this.edit = null;             // sprite edit mode: the image sprite being edited
    this.undoStack = [];          // {label, before, after}: C++ snapshot ids
    this.redoStack = [];
    this.stateId = 0;             // the snapshot that matches the current state
    this.dirty = false;
    this.regionCache = new Map(); // name -> canvas (original size), cleared on rebuild
    this.scheduleBuild = debounce(() => this.rebuild(), 100);
  }
  on(type, fn) { this.addEventListener(type, (e) => fn(e.detail)); }
  emit(type, detail) { this.dispatchEvent(new CustomEvent(type, { detail })); }

  // -- project ----------------------------------------------------------------------------------
  // Takes the project the C++ side now holds (new / opened / imported) as a fresh document.
  load(path, fileName, { dirty = false } = {}) {
    this.projectPath = path;
    this.fileName = fileName;
    this.project = this.ws.projectJson();
    this.variant = '';
    this.selection = [];
    this.edit = null;
    this.page = 0;
    this.undoStack = [];
    this.redoStack = [];
    this.stateId = this.ws.snapshot();
    this.gcSnapshots();
    this.dirty = dirty;
    this.emit('project');
    this.rebuild();
    this.emit('selection');
    this.emit('mode');
  }
  newProject() {
    this.ws.reset();
    this.ws.newProject('atlas', WORK + '/atlas.atlasproj');
    this.load(WORK + '/atlas.atlasproj', 'atlas.atlasproj');
  }

  // Changes the project's JSON: fn(project) edits it in place. One undo step per call.
  mutate(label, fn) {
    const before = JSON.stringify(this.project);
    fn(this.project);
    pruneEntries(this.project);
    if (before === JSON.stringify(this.project)) return false;
    try {
      this.ws.setProject(this.project, this.projectPath);
    } catch (e) {
      toast(String(e.message || e), 'error');
      this.project = this.ws.projectJson();
      this.emit('project');
      return false;
    }
    this.commit(label);
    return true;
  }
  // Records the C++ project as it is now (after mutate, or after an image operation: add,
  // replace, rename, delete, import) as one undo step.
  commit(label, { now = false } = {}) {
    this.project = this.ws.projectJson();
    const id = this.ws.snapshot();
    this.undoStack.push({ label, before: this.stateId, after: id });
    this.stateId = id;
    if (this.undoStack.length > HISTORY) this.undoStack.shift();
    this.redoStack = [];
    this.gcSnapshots();
    this.dirty = true;
    this.emit('project');
    if (now) this.rebuild(); else this.scheduleBuild();
  }
  // Only the snapshots the stacks (and the current state) use are kept.
  gcSnapshots() {
    const ids = new Set([this.stateId]);
    for (const u of [...this.undoStack, ...this.redoStack]) { ids.add(u.before); ids.add(u.after); }
    this.ws.keepSnapshots(ids);
  }
  undo() { this.step(this.undoStack, this.redoStack, 'before'); }
  redo() { this.step(this.redoStack, this.undoStack, 'after'); }
  step(from, to, side) {
    const u = from.pop();
    if (!u) return;
    to.push(u);
    this.ws.restore(u[side]);
    this.stateId = u[side];
    this.project = this.ws.projectJson();
    this.dirty = true;
    this.rebuild();
    const sel = this.selection.filter((n) => this.spriteExists(n));
    if (sel.length !== this.selection.length) { this.selection = sel; this.emit('selection'); }
    this.emit('project');
    toast((side === 'before' ? 'Undo: ' : 'Redo: ') + u.label);
  }

  spriteExists(name) { return this.resolvedMap.has(name) || !!this.entry(name); }
  entry(name) { return this.project.sprites.find((s) => s.name === name) || null; }
  ensureEntry(name) {
    let e = this.entry(name);
    if (!e) { e = { name }; this.project.sprites.push(e); }
    return e;
  }
  outputFor(variant = this.variant) {
    const v = variant && this.project.variants.find((x) => x.id === variant);
    return v ? v.output : this.project.output;
  }

  // -- image operations (C++ side, one undo step each) -------------------------------------------
  renameSprite(from, to) {
    to = (to || '').trim();
    if (!to || to === from) return false;
    const err = this.ws.renameSprite(from, to);
    if (err) { toast('Rename: ' + err, 'error'); return false; }
    this.selection = this.selection.map((n) => (n === from ? to : n));
    if (this.edit === from) this.edit = to;
    this.commit(`rename ${from} to ${to}`, { now: true });
    this.emit('selection');
    return true;
  }
  async replaceImage(name) {
    const items = await pickFiles({ multiple: false, accept: 'image/png,.png' });
    if (!items.length) return;
    const err = this.ws.setImageBytes(name, await itemBytes(items[0]), this.variant);
    if (err) { toast(err, 'error'); return; }
    this.commit(`replace image ${name}` + (this.variant ? ` (${this.variant})` : ''), { now: true });
    toast(`Replaced ${name}` + (this.variant ? ` in variant ${this.variant}` : ''));
  }
  saveImage(name) {
    const png = this.ws.regionPng(name);
    if (!png) { toast(name + ' is not in the atlas', 'error'); return; }
    download(png, baseName(name) + '.png', 'image/png');
  }
  async askRename(name) {
    const v = await promptDialog('Rename sprite', name, { okText: 'Rename' });
    if (v) this.renameSprite(name, v);
  }
  spriteMenu(name, x, y) {
    if (!this.selection.includes(name)) this.select([name]);
    const r = this.regions.get(name);
    const isChild = !!(this.entry(name)?.parent);
    contextMenu(x, y, [
      { text: 'Replace image...', disabled: isChild, action: () => this.replaceImage(name) },
      { text: 'Save image as PNG', disabled: !r, action: () => this.saveImage(name) },
      { text: 'Rename...', action: () => this.askRename(name) },
      null,
      { text: this.selection.length > 1 ? `Delete ${this.selection.length} sprites` : 'Delete', danger: true, action: () => deleteSelected() },
    ]);
  }

  // -- build ------------------------------------------------------------------------------------
  rebuild() {
    this.scheduleBuild.cancel();
    const t0 = performance.now();
    try {
      this.ws.setProject(this.project, this.projectPath);
      if (this.variant && !this.project.variants.some((v) => v.id === this.variant)) this.variant = '';
      this.result = this.ws.build(this.variant);
      const rs = this.ws.resolveSprites();
      this.resolved = rs.sprites;
    } catch (e) {
      this.result = { ok: false, pages: [], regions: [], animations: [], stats: {},
        diagnostics: [{ level: 'error', sprite: '', message: String(e.message || e) }] };
      this.resolved = [];
    }
    this.result.buildMs = performance.now() - t0;
    this.resolvedMap = new Map(this.resolved.map((s) => [s.name, s]));
    this.regions = new Map(this.result.regions.map((r) => [r.name, r]));
    this.regionCache.clear();
    this.pages = this.result.pages.map((p, i) => {
      const c = document.createElement('canvas');
      c.width = p.w; c.height = p.h;
      const rgba = this.ws.pageRGBA(i);
      if (rgba && p.w && p.h) c.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(rgba.buffer, rgba.byteOffset, rgba.length), p.w, p.h), 0, 0);
      return c;
    });
    if (this.page >= this.pages.length) this.page = Math.max(0, this.pages.length - 1);
    if (this.edit && !this.regions.has(this.edit)) this.setEdit(null);
    this.emit('build');
  }

  // A region drawn at its original size (trimmed pixels put back), cached until the next build.
  regionCanvas(name) {
    if (this.regionCache.has(name)) return this.regionCache.get(name);
    const img = this.ws.regionRGBA(name);
    let c = null;
    if (img && img.w > 0 && img.h > 0) {
      c = document.createElement('canvas');
      c.width = img.w; c.height = img.h;
      c.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(img.data.buffer, img.data.byteOffset, img.data.length), img.w, img.h), 0, 0);
    }
    this.regionCache.set(name, c);
    return c;
  }

  // -- selection / mode -------------------------------------------------------------------------
  select(names, { add = false, toggle = false } = {}) {
    names = names.filter(Boolean);
    let sel = add || toggle ? [...this.selection] : [];
    for (const n of names) {
      const i = sel.indexOf(n);
      if (toggle && i >= 0) sel.splice(i, 1);
      else { if (i >= 0) sel.splice(i, 1); sel.push(n); }
    }
    const same = sel.length === this.selection.length && sel.every((n, i) => n === this.selection[i]);
    this.selection = sel;
    // Show the page of the current sprite.
    const r = this.regions.get(this.current);
    if (r && !this.edit && r.page !== this.page) { this.page = r.page; this.emit('page'); }
    if (!same) this.emit('selection');
  }
  get current() { return this.selection[this.selection.length - 1] || null; }

  setEdit(name) {
    if (name) {
      const r = this.regions.get(name);
      if (r && r.child && !r.baked) name = r.parent;   // a child is edited inside its parent
    }
    if (name && !this.regions.has(name)) name = null;
    if (this.edit === name) return;
    this.edit = name;
    this.emit('mode');
  }
}

// Entries that hold nothing but a name are dropped.
function pruneEntries(p) {
  p.sprites = p.sprites.filter((s) => Object.keys(s).some((k) => k !== 'name' && s[k] !== undefined));
}

// ---- boot ---------------------------------------------------------------------------------------
const logLines = [];
function log(text, kind = 'info') {
  logLines.push({ text, kind });
  if (logLines.length > 500) logLines.shift();
  window.dispatchEvent(new CustomEvent('atlas-log'));
}

let app;
async function main() {
  applyTheme(prefs().theme || 'dark');
  let ws;
  try {
    ws = await loadWorkspace(log);
  } catch (e) {
    $('#loading').textContent = 'Could not load atlas_web.wasm: ' + (e.message || e) +
      '. Serve this folder over http (python tools/serve_web.py <folder>), not file://.';
    return;
  }
  app = new App(ws);
  window.atlasApp = app;   // for debugging and automated checks
  app.deleteSelected = deleteSelected;
  app.log = log;
  app.ingest = ingest;
  app.saveAll = saveAll;
  new SpritesPanel(app, $('#left'));
  const view = new AtlasView(app, $('#center'));
  window.atlasView = view;
  new PropsPanel(app, $('#right'));
  new AnimsPanel(app, $('#anims'));
  setupProblems();
  setupLog();
  setupToolbar(view);
  setupDrop();
  setupSplitters();
  setupKeys(view);
  setupStatus();
  app.on('project', updateTitle);
  $('#loading').remove();
  app.newProject();
  window.atlasReady = true;
}

function updateTitle() {
  document.title = (app.dirty ? '* ' : '') + app.fileName + ' - TOMS Atlas';
  $('#undoBtn').disabled = !app.undoStack.length;
  $('#redoBtn').disabled = !app.redoStack.length;
}

// ---- theme --------------------------------------------------------------------------------------
const THEMES = ['dark', 'light', 'auto'];
function applyTheme(t) {
  document.documentElement.dataset.theme = t;
  const b = $('#themeBtn');
  if (b) b.textContent = { dark: 'Dark', light: 'Light', auto: 'Auto' }[t];
}

// ---- toolbar ------------------------------------------------------------------------------------
function setupToolbar(view) {
  const variantSel = $('#variantSel');
  const fillVariants = () => {
    variantSel.replaceChildren(el('option', { value: '' }, 'base art'),
      ...app.project.variants.map((v) => el('option', { value: v.id }, 'variant: ' + v.id)));
    variantSel.value = app.variant;
    variantSel.disabled = !app.project.variants.length;
  };
  app.on('project', fillVariants);
  variantSel.onchange = () => { app.variant = variantSel.value; app.rebuild(); app.emit('project'); };

  $('#newBtn').onclick = async () => {
    if (app.dirty && !(await confirmDialog('New project', 'Discard the unsaved changes?', 'Discard'))) return;
    app.newProject();
  };
  const picked = (input, opts) => async (e) => {
    const items = collectInput(e.target.files);
    e.target.value = '';
    try { await ingest(items, opts); } catch (err) { toast(String(err.message || err), 'error'); log(String(err.stack || err), 'error'); }
  };
  $('#openBtn').onclick = () => $('#openInput').click();
  $('#openFolderBtn').onclick = () => $('#openFolderInput').click();
  $('#openInput').onchange = picked('#openInput', { open: true });
  $('#openFolderInput').onchange = picked('#openFolderInput', { open: true });
  $('#saveBtn').onclick = saveAll;
  $('#importBtn').onclick = () => $('#importInput').click();
  $('#importInput').onchange = async (e) => {
    const items = collectInput(e.target.files);
    e.target.value = '';
    await importAtlas(items);
  };
  $('#exportBtn').onclick = exportAtlas;
  $('#extractBtn').onclick = extractImages;
  $('#refsBtn').onclick = () => referencesDialog(app);
  $('#addFilesBtn').onclick = () => $('#filesInput').click();
  $('#addFolderBtn').onclick = () => $('#folderInput').click();
  for (const id of ['#filesInput', '#folderInput']) $(id).onchange = picked(id, {});
  $('#undoBtn').onclick = () => app.undo();
  $('#redoBtn').onclick = () => app.redo();
  $('#themeBtn').onclick = () => {
    const t = THEMES[(THEMES.indexOf(document.documentElement.dataset.theme) + 1) % THEMES.length];
    setPref('theme', t);
    applyTheme(t);
    view.draw();
  };
  applyTheme(prefs().theme || 'dark');
  window.addEventListener('beforeunload', (e) => { if (app.dirty) { e.preventDefault(); e.returnValue = ''; } });
}

// ---- save / export / extract --------------------------------------------------------------------
// Files below `root` as zip entries named relative to their deepest common folder.
function zipEntries(root, first = '') {
  const files = app.ws.list(root);
  if (!files.length) return [];
  let common = dirName(files[0]).split('/');
  for (const f of files) {
    const d = dirName(f).split('/');
    let i = 0;
    while (i < common.length && i < d.length && common[i] === d[i]) i++;
    common = common.slice(0, i);
  }
  const base = common.join('/');
  const out = files.map((f) => ({ name: f.slice(base.length + 1), data: app.ws.read(f) }));
  if (first) out.sort((a, b) => (b.name.endsWith(first) - a.name.endsWith(first)));
  return out;
}

// Save: saveProjectAll() into /save (the project, its packed atlas and pages, the other formats,
// every variant's atlas, laid out as the project's relative paths say), downloaded as one .zip.
function saveAll() {
  const ws = app.ws;
  const rel = app.projectPath.startsWith(WORK + '/') ? app.projectPath.slice(WORK.length + 1) : baseName(app.projectPath);
  // Deep enough that output folders like ../styles/x/atlas still land inside /save.
  let target = SAVE + '/' + rel;
  for (let fill = 'project/'; fill.length < 200; fill += 'project/') {
    if (ws.outputDirs(target).every((d) => d === SAVE || d.startsWith(SAVE + '/'))) break;
    target = SAVE + '/' + fill + rel;
  }
  ws.remove(SAVE);
  const r = ws.saveAll(target, [ALWAYS_EXPORTED]);
  if (r.error) {
    ws.remove(SAVE);
    toast('Not saved: ' + r.error, 'error');
    log('save: ' + r.error, 'error');
    return false;
  }
  const entries = zipEntries(SAVE, '.atlasproj');
  ws.remove(SAVE);
  for (const d of r.diagnostics || []) if (d.level !== 'info') log(`save: ${d.level}: ${d.sprite ? d.sprite + ': ' : ''}${d.message}`, d.level);
  const zipName = stem(app.fileName) + '.zip';
  download(makeZip(entries), zipName);
  log(`saved ${zipName}: ${entries.map((e) => e.name).join(', ')}`);
  app.dirty = false;
  updateTitle();
  toast(`Saved ${zipName} (${entries.length} files: project + packed atlas)`);
  return true;
}

function exportAtlas() {
  if (!app.result || !app.result.ok) { toast('Fix the errors in Problems before exporting', 'error'); return; }
  // Export always writes the Cocos .plist too (Cocos Creator's Sprite Atlas); Properties shows it as always on.
  const formats = [...new Set([...app.outputFor().formats, ALWAYS_EXPORTED])];
  const files = app.ws.exportFiles(formats);
  if (files.error) { toast(files.error, 'error'); return; }
  const name = app.result.name || 'atlas';
  if (files.length === 1) download(files[0].data, files[0].name);
  else download(makeZip(files), name + (app.variant ? '_' + app.variant : '') + '.zip');
  toast(`Exported ${files.length} file(s): ${formats.join(', ')}`);
}

async function extractImages() {
  const v = app.variant;
  const res = await choiceDialog('Extract images',
    `Every image${v ? ' of variant ' + v : ''} at its original size as <name>.png, in one .zip.`,
    [{ key: 'ok', text: 'Extract', primary: true }], [{ key: 'children', text: 'Include child sprites' }]);
  if (res.button !== 'ok') return;
  app.ws.remove(EXTRACT);
  const r = app.ws.extract(EXTRACT, v, res.checks.children);
  if (r.error) { app.ws.remove(EXTRACT); toast('Extract: ' + r.error, 'error'); return; }
  const files = app.ws.list(EXTRACT).map((f) => ({ name: f.slice(EXTRACT.length + 1), data: app.ws.read(f) }));
  app.ws.remove(EXTRACT);
  if (!files.length) { toast('No images to extract', 'error'); return; }
  download(makeZip(files), stem(app.fileName) + (v ? '_' + v : '') + '_images.zip');
  toast(`Extracted ${files.length} image(s)`);
}

// ---- files in -----------------------------------------------------------------------------------
const isPng = (p) => ext(p) === 'png';

// Dropped/picked files: a .zip is unpacked first. An .atlasproj among them opens that project (with
// the packed atlas and folders that came along); an atlas file (.pi/.atlas/.json + PNG) without a
// project is imported; PNGs and folders are added as images.
async function ingest(items, { open = false } = {}) {
  if (!items.length) return;
  const zips = items.filter((i) => ext(i.rel) === 'zip');
  if (zips.length) {
    const rest = items.filter((i) => ext(i.rel) !== 'zip');
    for (const z of zips) {
      let entries;
      try { entries = await unzip(await itemBytes(z)); } catch (e) { toast(`${z.rel}: ${e.message || e}`, 'error'); return; }
      for (const e of entries) rest.push({ rel: e.name, data: e.data });
    }
    items = rest;
  }
  const projs = items.filter((i) => ext(i.rel) === 'atlasproj')
    .sort((a, b) => a.rel.split('/').length - b.rel.split('/').length || a.rel.localeCompare(b.rel));
  if (projs.length) { await openItems(items, projs[0]); return; }
  if (open) { toast('Pick a .atlasproj with its packed atlas (.atlas + PNG pages), its folder, or a .zip made by Save', 'error'); return; }
  const atlasFile = items.find((i) => ['pi', 'atlas'].includes(ext(i.rel)) ||
    (ext(i.rel) === 'json' && !i.rel.includes('/') && items.some((j) => isPng(j.rel) && !j.rel.includes('/'))));
  if (atlasFile) { await importAtlas(items); return; }
  await addImages(items);
}

// "a/b" relative to the folder "from" (both absolute MEMFS paths).
function relPath(from, to) {
  const a = from.split('/').filter(Boolean), b = to.split('/').filter(Boolean);
  let i = 0;
  while (i < a.length && i < b.length && a[i] === b[i]) i++;
  return [...a.slice(i).map(() => '..'), ...b.slice(i)].join('/') || '.';
}

// Opens `proj` (an .atlasproj among `items`): every item is written to /work/<its relative path>,
// then the core opens it. Missing packed atlases / source folders can still be chosen here.
async function openItems(items, proj) {
  if (app.dirty && !(await confirmDialog('Open project', 'Discard the unsaved changes?', 'Discard'))) return;
  const ws = app.ws;
  ws.reset();
  for (const it of items) ws.write(WORK + '/' + it.rel, await itemBytes(it));
  const path = WORK + '/' + proj.rel;
  const fileName = baseName(proj.rel);
  const here = dirName(path);
  const fail = (msg) => { toast(`Cannot open ${fileName}: ${msg}`, 'error'); log(`open ${proj.rel}: ${msg}`, 'error'); };
  let info = ws.inspect(path);
  if (info.error) { fail(info.error); return; }
  let dropMissing = false;
  if (info.embedded) {
    if (!info.storeExists) {
      const res = await folderSlotsDialog(app, { title: 'Open ' + fileName,
        text: 'The packed atlas holds this project\'s images, and it was not picked along. Select it (the .atlas with its PNG pages) together with the project, or choose its folder here.',
        slots: [{ label: 'packed atlas', path: relPath(here, dirName(info.store)), dir: dirName(info.store), need: 'atlas' }], okText: 'Open' });
      if (!res) return;
      info = ws.inspect(path);
      if (!info.storeExists) { fail(`the packed atlas ${relPath(here, info.store)} is missing (it holds this project's images)`); return; }
    }
    const missing = info.variants.filter((v) => v.needed && !v.exists);
    if (missing.length) {
      const res = await folderSlotsDialog(app, { title: 'Open ' + fileName,
        text: 'These variants keep their art in their own packed atlas, which was not picked along. Choose each folder, or open without that art (Save would then write those variants without it).',
        slots: missing.map((v) => ({ label: 'variant ' + v.id, path: relPath(here, v.dir), dir: v.dir, need: 'atlas' })),
        okText: 'Open', altText: 'Open without that art' });
      if (!res) return;
      dropMissing = true;
    }
  } else {
    const miss = [
      ...info.sources.filter((s) => !s.pngs).map((s) => ({ label: 'source folder', path: s.path, dir: s.dir, need: 'png' })),
      ...info.overrides.filter((s) => !s.pngs).map((s) => ({ label: 'variant ' + s.id, path: s.path, dir: s.dir, need: 'png' })),
    ];
    if (miss.length) {
      const res = await folderSlotsDialog(app, { title: 'Open ' + fileName,
        text: 'This is a folder project: its images are read from these folders, and it will be converted to an embedded project (the images go into the packed atlas when you save). Choose the folders that were not picked along.',
        slots: miss, okText: 'Convert' });
      if (!res) return;
    }
  }
  const r = ws.openProject(path, dropMissing);
  if (r.error) { fail(r.error); return; }
  app.load(path, fileName, { dirty: r.converted >= 0 });
  for (const d of r.diagnostics || []) log(`${fileName}: ${d.level}: ${d.sprite ? d.sprite + ': ' : ''}${d.message}`, d.level);
  for (const id of r.dropped || []) log(`${fileName}: variant ${id}: its packed atlas was not loaded; it has no replacement art now`, 'warning');
  if (r.converted >= 0) {
    const msg = `${fileName} was a folder project: ${r.converted} image(s) taken in, it is now embedded. Save writes it with its packed atlas; the folders became references.`;
    log(msg, 'warning');
    toast(msg);
  } else {
    log(`opened ${proj.rel}: ${r.images} image(s)`);
    toast(`Opened ${fileName}: ${r.images} image(s)` + (r.dropped?.length ? ` (without the art of ${r.dropped.join(', ')})` : ''));
  }
}

// PNGs (loose files or folders) become images: the name is the file name without .png, or the path
// below the dropped folder. With a variant picked in the toolbar they become its replacement art.
async function addImages(items) {
  const ws = app.ws;
  const pngs = items.filter((i) => isPng(i.rel));
  if (!pngs.length) { toast('No PNG files in what was dropped', 'error'); return; }
  const variant = app.variant;
  const plan = pngs.map((it) => {
    const top = it.rel.includes('/') ? it.rel.slice(0, it.rel.indexOf('/')) : '';
    return { it, name: ws.imageNameFor(it.rel, top, '') };
  });
  let all = null;
  const todo = [];
  for (const p of plan) {
    if (ws.hasImage(p.name, variant)) {
      let act = all;
      if (!act) {
        const r = await choiceDialog('Image exists', `"${p.name}" is already in the ${variant ? 'variant ' + variant : 'project'}.`,
          [{ key: 'skip', text: 'Skip' }, { key: 'replace', text: 'Replace', primary: true }], [{ key: 'all', text: 'Apply to all' }]);
        if (!r.button) return;
        act = r.button;
        if (r.checks.all) all = act;
      }
      if (act === 'skip') continue;
    }
    todo.push(p);
  }
  const errors = [];
  let n = 0;
  for (const p of todo) {
    const e = ws.setImageBytes(p.name, await itemBytes(p.it), variant);
    if (e) errors.push(e); else n++;
  }
  if (n) app.commit(`add ${n} image(s)` + (variant ? ` (${variant})` : ''), { now: true });
  for (const e of errors) log('add: ' + e, 'error');
  if (errors.length) toast(`Added ${n} image(s), ${errors.length} failed (see Log)`, 'error');
  else if (n) toast(`Added ${n} image(s)` + (variant ? ` to variant ${variant}` : ''));
  else toast('Nothing added');
}

async function importAtlas(items) {
  const atlas = items.find((i) => ['pi', 'atlas', 'json', 'txt'].includes(ext(i.rel)));
  if (!atlas) { toast('Pick the atlas file (.pi, .atlas or .json) together with its PNG page(s)', 'error'); return; }
  if (app.dirty && !(await confirmDialog('Import atlas', 'Discard the unsaved changes?', 'Discard'))) return;
  const ws = app.ws;
  ws.reset();
  const inDir = '/import';
  for (const it of items) ws.write(inDir + '/' + baseName(it.rel), await itemBytes(it));
  const name = stem(atlas.rel);
  const projPath = WORK + '/' + name + '.atlasproj';
  const r = ws.importAtlas(inDir + '/' + baseName(atlas.rel), projPath);
  ws.remove(inDir);
  if (r.error) { toast('Import failed: ' + r.error, 'error'); return; }
  app.load(projPath, name + '.atlasproj', { dirty: true });
  for (const d of r.diagnostics) log(`${atlas.rel}: ${d.level}: ${d.sprite ? d.sprite + ': ' : ''}${d.message}`, d.level);
  toast(`Imported ${r.sprites} sprite(s) (${r.children} child) - Save writes the project with its packed atlas`);
}

function setupDrop() {
  const hint = $('#dropHint');
  let depth = 0;
  window.addEventListener('dragenter', (e) => { if ([...e.dataTransfer.types].includes('Files')) { depth++; hint.classList.add('show'); } });
  window.addEventListener('dragleave', () => { depth = Math.max(0, depth - 1); if (!depth) hint.classList.remove('show'); });
  window.addEventListener('dragover', (e) => { if ([...e.dataTransfer.types].includes('Files')) e.preventDefault(); });
  window.addEventListener('drop', async (e) => {
    if (![...e.dataTransfer.types].includes('Files')) return;
    e.preventDefault();
    depth = 0;
    hint.classList.remove('show');
    if ($('.dialog-back')) return;
    const pending = collectDrop(e.dataTransfer);   // synchronously, before any await
    try { await ingest(await pending); } catch (err) { toast(String(err.message || err), 'error'); }
  });
}

// ---- keys ---------------------------------------------------------------------------------------
function setupKeys(view) {
  window.addEventListener('keydown', (e) => {
    const mod = e.ctrlKey || e.metaKey;
    if ($('.dialog-back') || $('#ctxMenu')) return;
    if (mod && !typingInField()) {
      const k = e.key.toLowerCase();
      if (k === 'z' && !e.shiftKey) { e.preventDefault(); app.undo(); return; }
      if (k === 'y' || (k === 'z' && e.shiftKey)) { e.preventDefault(); app.redo(); return; }
    }
    if (mod && e.key.toLowerCase() === 's') { e.preventDefault(); saveAll(); return; }
    if (mod && e.key.toLowerCase() === 'e') { e.preventDefault(); exportAtlas(); return; }
    if (mod && e.key.toLowerCase() === 'o') { e.preventDefault(); (e.shiftKey ? $('#openFolderInput') : $('#openInput')).click(); return; }
    if (typingInField()) return;
    if (e.key === 'Delete' || e.key === 'Backspace') { e.preventDefault(); deleteSelected(); }
    else if (e.key === 'F2' && app.current) { e.preventDefault(); app.askRename(app.current); }
    else if (e.key === 'Escape') { if (app.edit) app.setEdit(null); else app.select([]); }
    else if (e.key === 'Enter' && app.current) app.setEdit(app.current);
    else view.key(e);
  });
}

// Deletes the selected sprites with their children: images, variant art, settings, animation
// frames. One undo step.
async function deleteSelected({ confirm = true } = {}) {
  const names = [...app.selection];
  if (!names.length) return;
  const victims = new Set(names);
  for (let more = true; more;) {
    more = false;
    for (const s of app.project.sprites) if (s.parent && victims.has(s.parent) && !victims.has(s.name)) { victims.add(s.name); more = true; }
  }
  const kids = victims.size - names.length;
  const what = names.length === 1 ? `"${names[0]}"` : `${names.length} sprites`;
  if (confirm && !(await confirmDialog('Delete sprites',
    `Delete ${what}${kids ? ` and ${kids} child sprite(s)` : ''}? The image, its variant art and its settings go (Undo brings them back).`, 'Delete'))) return;
  const n = app.ws.deleteSprites([...victims]);
  if (!n) return;
  app.select([]);
  app.commit(`delete ${what}`, { now: true });
}

// ---- problems / log / status ------------------------------------------------------------------
function setupProblems() {
  const list = $('#problems');
  const tab = $('#tab-problems');
  app.on('build', () => {
    const ds = app.result.diagnostics;
    const errors = ds.filter((d) => d.level === 'error').length, warnings = ds.filter((d) => d.level === 'warning').length;
    tab.textContent = 'Problems' + (ds.length ? ` (${ds.length})` : '');
    tab.classList.toggle('has-errors', errors > 0);
    tab.classList.toggle('has-warnings', !errors && warnings > 0);
    list.replaceChildren(...(ds.length ? ds.map((d) => el('li', {
      class: 'diag ' + d.level, tabindex: '0',
      onclick: () => { if (d.sprite) app.select([d.sprite]); },
      onkeydown: (e) => { if (e.key === 'Enter' && d.sprite) app.select([d.sprite]); },
    }, el('span.lv', {}, d.level), d.sprite ? el('b', {}, d.sprite) : null, ' ', d.message))
      : [el('li.empty', {}, 'No problems.')]));
  });
}

function setupLog() {
  const list = $('#log');
  const render = () => list.replaceChildren(...logLines.map((l) => el('div', { class: l.kind }, l.text)));
  window.addEventListener('atlas-log', debounce(render, 50));
  render();
}

function setupStatus() {
  const s = $('#status');
  const render = () => {
    const r = app.result;
    if (!r) return;
    const st = r.stats || {};
    const pages = r.pages.map((p) => `${p.w}x${p.h}`).join(', ');
    s.replaceChildren(
      el('span', {}, `${app.resolved.length} sprite(s)`),
      el('span', {}, `${st.images || 0} packed, ${st.children || 0} child, ${st.aliases || 0} dedupe`),
      el('span', {}, `${r.pages.length} page(s)${pages ? ': ' + pages : ''}`),
      el('span', {}, `occupancy ${Math.round((st.occupancy || 0) * 100)}%`),
      el('span', {}, `build ${Math.round(r.buildMs || 0)} ms`),
      el('span.grow'),
      el('span', { class: r.ok ? 'ok' : 'bad' }, r.ok ? 'ready to export' : `${st.errors || 0} error(s)`));
  };
  app.on('build', render);
}

// ---- splitters / bottom tabs --------------------------------------------------------------------
function setupSplitters() {
  const root = document.documentElement;
  const p = prefs();
  const sizes = { left: p.leftW || 260, right: p.rightW || 300, bottom: p.bottomH || 190 };
  const apply = () => {
    root.style.setProperty('--left-w', sizes.left + 'px');
    root.style.setProperty('--right-w', sizes.right + 'px');
    root.style.setProperty('--bottom-h', sizes.bottom + 'px');
    window.dispatchEvent(new Event('resize'));
  };
  apply();
  for (const sp of $$('.splitter')) {
    sp.addEventListener('pointerdown', (e) => {
      e.preventDefault();
      sp.setPointerCapture(e.pointerId);
      const which = sp.dataset.split, x0 = e.clientX, y0 = e.clientY, s0 = sizes[which];
      const move = (ev) => {
        if (which === 'left') sizes.left = Math.min(600, Math.max(160, s0 + ev.clientX - x0));
        if (which === 'right') sizes.right = Math.min(640, Math.max(220, s0 - (ev.clientX - x0)));
        if (which === 'bottom') sizes.bottom = Math.min(window.innerHeight - 200, Math.max(60, s0 - (ev.clientY - y0)));
        apply();
      };
      const up = () => {
        sp.removeEventListener('pointermove', move);
        sp.removeEventListener('pointerup', up);
        setPref('leftW', sizes.left); setPref('rightW', sizes.right); setPref('bottomH', sizes.bottom);
      };
      sp.addEventListener('pointermove', move);
      sp.addEventListener('pointerup', up);
    });
  }
  const tabs = $$('#bottomTabs button');
  const show = (id) => {
    for (const t of tabs) t.classList.toggle('active', t.dataset.tab === id);
    for (const pane of $$('#bottom .pane')) pane.hidden = pane.id !== id;
    setPref('bottomTab', id);
    window.dispatchEvent(new CustomEvent('atlas-tab', { detail: id }));
  };
  for (const t of tabs) t.onclick = () => show(t.dataset.tab);
  show(prefs().bottomTab || 'problems');
}

main();
