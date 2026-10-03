// refs.js -- folders the browser has to be handed: the "choose this folder" dialog used when opening
// a project whose variant atlas or (folder project) source folders were not picked along, and the
// "Import from references..." dialog (scanReferences: New / Changed / Same, import the checked ones).
//
// A browser cannot keep a folder path, so a reference folder is loaded for the session: its files
// are written into MEMFS where the project's (relative) reference path points.
import { collectDrop } from './wasm.js';
import { el, toast, pickFiles, promptDialog, readBytes, ext, naturalCompare } from './util.js';

export const itemBytes = (it) => (it.data ? Promise.resolve(it.data) : readBytes(it.file));

// Writes picked/dropped items into `dir`. Items from one folder ("sprites/a.png", "sprites/x/b.png")
// lose that top folder; loose files keep their names. Returns the number of files written.
export async function writeFolderItems(ws, items, dir, { clear = true } = {}) {
  if (!items.length) return 0;
  const tops = new Set(items.map((i) => (i.rel.includes('/') ? i.rel.slice(0, i.rel.indexOf('/')) : '')));
  const strip = tops.size === 1 && !tops.has('');
  if (clear) ws.remove(dir);
  for (const it of items) {
    const rel = strip ? it.rel.slice(it.rel.indexOf('/') + 1) : it.rel;
    ws.write(dir + '/' + rel, await itemBytes(it));
  }
  return items.length;
}

// The top folder name of picked/dropped items ('' for loose files).
export function topFolder(items) {
  const r = items.find((i) => i.rel.includes('/'));
  return r ? r.rel.slice(0, r.rel.indexOf('/')) : '';
}

const pngCount = (ws, dir) => ws.list(dir).filter((f) => ext(f) === 'png').length;

// Lets the user choose folders that belong at known places. slots: [{label, path, dir, need:
// 'atlas'|'png'}]. Resolves to 'ok', 'alt' (the alternative button) or null (cancelled).
export function folderSlotsDialog(app, { title, text, slots, okText = 'Continue', altText = '' }) {
  return new Promise((resolve) => {
    const ws = app.ws;
    const list = el('div.slots');
    const okBtn = el('button.primary#slotsOk', { type: 'button', onclick: () => close('ok') }, okText);
    const status = (s) => {
      if (s.need === 'atlas') return ws.list(s.dir).some((f) => ext(f) === 'atlas') ? 'loaded' : '';
      const n = pngCount(ws, s.dir);
      return n ? `${n} PNG(s) loaded` : '';
    };
    const load = async (s, items) => {
      if (!items.length) return;
      await writeFolderItems(ws, items, s.dir);
      render();
    };
    const render = () => {
      list.replaceChildren(...slots.map((s, i) => {
        const st = status(s);
        const row = el('div.slot', { dataset: { slot: String(i) } },
          el('div', {}, el('b', {}, s.label), ' ', el('code', { title: s.dir }, s.path)),
          el('div.line', {}, el('span', { class: st ? 'ok' : 'bad' }, st || 'not loaded'), el('span.grow'),
            el('button.choose', { type: 'button', onclick: async () => load(s, await pickFiles({ directory: true })) }, 'Choose folder...')));
        row.addEventListener('dragover', (e) => { e.preventDefault(); e.stopPropagation(); row.classList.add('drop'); });
        row.addEventListener('dragleave', () => row.classList.remove('drop'));
        row.addEventListener('drop', async (e) => {
          e.preventDefault(); e.stopPropagation(); row.classList.remove('drop');
          await load(s, await collectDrop(e.dataTransfer));
        });
        return row;
      }));
    };
    const close = (v) => { back.remove(); resolve(v); };
    const form = el('div.dialog.wide#slotsDialog', { tabindex: '-1' },
      el('div.dialog-title', {}, title), text ? el('p', {}, text) : null, list,
      el('div.dialog-buttons', {},
        el('button', { type: 'button', onclick: () => close(null) }, 'Cancel'),
        altText ? el('button#slotsAlt', { type: 'button', onclick: () => close('alt') }, altText) : null, okBtn));
    const back = el('div.dialog-back', {}, form);
    form.addEventListener('keydown', (e) => { if (e.key === 'Escape') { e.stopPropagation(); close(null); } });
    back.addEventListener('dragover', (e) => e.preventDefault());
    back.addEventListener('drop', (e) => { e.preventDefault(); e.stopPropagation(); if (slots.length !== 1) toast('Drop the folder onto its row'); });
    render();
    document.body.append(back);
    form.focus();
  });
}

// ---- Import from references -------------------------------------------------------------------

const STATUS_ORDER = { new: 0, changed: 1, same: 2 };

export function referencesDialog(app) {
  const ws = app.ws;
  const variant = app.variant;
  let rows = [];
  const checked = new Set();
  const urls = [];
  const folderBox = el('div.slots');
  const summary = el('span.summary');
  const table = el('tbody');
  const importBtn = el('button.primary#refsImport', { type: 'button', onclick: () => doImport() }, 'Import');
  const allBox = el('input', { type: 'checkbox', title: 'all / none', onchange: () => {
    for (const r of rows) { if (allBox.checked) checked.add(r.name); else checked.delete(r.name); }
    renderRows();
  } });

  const folders = () => {
    const v = variant && app.project.variants.find((x) => x.id === variant);
    if (variant) return v && v.reference ? [{ path: v.reference, recursive: true, prefix: '' }] : [];
    return app.project.references || [];
  };

  const loadInto = async (path, items) => {
    if (!items.length) return;
    await writeFolderItems(ws, items, ws.resolvePath(path));
    scan();
  };
  // A new reference folder (base art), or the variant's reference: its path relative to the project.
  const addFolder = async (items) => {
    if (!items.length) return;
    const name = topFolder(items) || 'references';
    const path = await promptDialog('Where this folder is, relative to the project (kept in the project file)', name);
    if (!path) return;
    if (variant) app.mutate(`reference of ${variant}`, (p) => { p.variants.find((x) => x.id === variant).reference = path; });
    else if (!folders().some((f) => f.path === path)) app.mutate('add reference folder', (p) => { (p.references ||= []).push({ path, recursive: true, prefix: '' }); });
    await loadInto(path, items);
  };

  const renderFolders = () => {
    const fs = folders();
    folderBox.replaceChildren(
      ...fs.map((f) => {
        const dir = ws.resolvePath(f.path);
        const n = pngCount(ws, dir);
        const row = el('div.slot', {},
          el('div', {}, el('code', { title: dir }, f.path), f.prefix ? el('span.k', {}, ' prefix ' + f.prefix) : null,
            f.recursive === false ? el('span.k', {}, ' (no subfolders)') : null),
          el('div.line', {}, el('span', { class: n ? 'ok' : 'bad' }, n ? `${n} PNG(s) loaded` : 'not loaded this session'), el('span.grow'),
            el('button.choose', { type: 'button', onclick: async () => loadInto(f.path, await pickFiles({ directory: true })) }, 'Choose folder...')));
        row.addEventListener('dragover', (e) => { e.preventDefault(); e.stopPropagation(); row.classList.add('drop'); });
        row.addEventListener('dragleave', () => row.classList.remove('drop'));
        row.addEventListener('drop', async (e) => {
          e.preventDefault(); e.stopPropagation(); row.classList.remove('drop');
          await loadInto(f.path, await collectDrop(e.dataTransfer));
        });
        return row;
      }),
      (!variant || !fs.length) ? el('div.line', {},
        el('button#refsAdd', { type: 'button', onclick: async () => addFolder(await pickFiles({ directory: true })) },
          variant ? 'Set reference folder...' : '+ Reference folder...'),
        el('span.k', {}, fs.length ? '' : variant ? ' this variant has no reference folder yet' : ' the project has no reference folders yet')) : null);
  };

  const thumb = (src) => {
    const c = el('canvas.thumb', { width: 32, height: 32 });
    if (!src) return c;
    const ctx = c.getContext('2d');
    const w = src.naturalWidth || src.width, h = src.naturalHeight || src.height;
    if (!w || !h) return c;
    const s = Math.min(32 / w, 32 / h, 4);
    ctx.imageSmoothingEnabled = s < 1;
    ctx.drawImage(src, (32 - w * s) / 2, (32 - h * s) / 2, w * s, h * s);
    return c;
  };
  const fileThumb = (file) => {
    const c = el('canvas.thumb', { width: 32, height: 32 });
    const bytes = ws.read(file);
    if (!bytes) return c;
    const url = URL.createObjectURL(new Blob([bytes], { type: 'image/png' }));
    urls.push(url);
    const img = new Image();
    img.onload = () => c.replaceWith(Object.assign(thumb(img), { title: 'reference' }));
    img.src = url;
    return c;
  };

  const renderRows = () => {
    const counts = { new: 0, changed: 0, same: 0 };
    for (const r of rows) counts[r.status]++;
    summary.textContent = rows.length ? `${counts.new} new, ${counts.changed} changed, ${counts.same} same` : 'No PNGs in the reference folder(s) loaded.';
    importBtn.textContent = `Import ${checked.size}`;
    importBtn.disabled = !checked.size;
    allBox.checked = rows.length > 0 && checked.size === rows.length;
    for (const tr of table.children) {
      const box = tr.querySelector('input');
      if (box) box.checked = checked.has(tr.dataset.name);
    }
  };
  const scan = () => {
    renderFolders();
    rows = ws.scanReferences(variant).sort((a, b) => STATUS_ORDER[a.status] - STATUS_ORDER[b.status] || naturalCompare(a.name, b.name));
    checked.clear();
    for (const r of rows) if (r.status !== 'same') checked.add(r.name);
    for (const u of urls.splice(0)) URL.revokeObjectURL(u);
    table.replaceChildren(...rows.map((r) => {
      const box = el('input', { type: 'checkbox', onchange: () => { if (box.checked) checked.add(r.name); else checked.delete(r.name); renderRows(); } });
      return el('tr', { dataset: { name: r.name, status: r.status } },
        el('td', {}, box),
        el('td', {}, el('span', { class: 'status ' + r.status }, r.status === 'new' ? 'New' : r.status === 'changed' ? 'Changed' : 'Same')),
        el('td', {}, fileThumb(r.file)),
        el('td', {}, r.status === 'new' ? el('span.k', {}, '-') : thumb(app.regionCanvas(r.name))),
        el('td.name', {}, r.name),
        el('td.file', { title: r.file }, ws.relativize(r.file)));
    }));
    renderRows();
  };

  const doImport = () => {
    const pick = rows.filter((r) => checked.has(r.name));
    if (!pick.length) return;
    const errors = [];
    for (const r of pick) {
      const e = ws.setImageFile(r.name, r.file, variant);
      if (e) errors.push(e);
    }
    const ok = pick.length - errors.length;
    if (ok) app.commit(`import ${ok} image(s) from references` + (variant ? ` (${variant})` : ''));
    for (const e of errors) app.log('import: ' + e, 'error');
    toast(errors.length ? `Imported ${ok} image(s), ${errors.length} failed (see Log)` : `Imported ${ok} image(s)`, errors.length ? 'error' : 'info');
    close();
  };

  const close = () => { for (const u of urls) URL.revokeObjectURL(u); back.remove(); };
  const form = el('div.dialog.wide.refs#refsDialog', { tabindex: '-1' },
    el('div.dialog-title', {}, 'Import from references' + (variant ? ` - variant ${variant}` : ' - base art')),
    el('p', {}, variant
      ? 'Replacement art for this variant, by sprite name. Only sprites the base art has are listed.'
      : 'PNGs in the reference folders, compared with the images in the project. Choose or drop a folder to load it for this session.'),
    folderBox,
    el('div.line', {}, summary),
    el('div.tablebox', {}, el('table', {},
      el('thead', {}, el('tr', {}, el('th', {}, allBox), el('th', {}, 'Status'), el('th', {}, 'File'), el('th', {}, 'Now'), el('th', {}, 'Name'), el('th', {}, 'Path'))),
      table)),
    el('div.dialog-buttons', {}, el('button', { type: 'button', onclick: close }, 'Close'), importBtn));
  const back = el('div.dialog-back', {}, form);
  form.addEventListener('keydown', (e) => { if (e.key === 'Escape') { e.stopPropagation(); close(); } });
  back.addEventListener('dragover', (e) => e.preventDefault());
  back.addEventListener('drop', async (e) => {
    e.preventDefault(); e.stopPropagation();
    const items = await collectDrop(e.dataTransfer);
    const fs = folders();
    if (fs.length === 1) await loadInto(fs[0].path, items);
    else if (!fs.length) await addFolder(items);
    else toast('Drop the folder onto the reference it belongs to');
  });
  document.body.append(back);
  scan();
  form.focus();
}
