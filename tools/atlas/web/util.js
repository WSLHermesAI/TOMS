// util.js -- small DOM / file helpers shared by the atlas web editor.

export const $ = (sel, root = document) => root.querySelector(sel);
export const $$ = (sel, root = document) => [...root.querySelectorAll(sel)];

// el('div.cls#id', {attr: v, onclick: fn, style: {...}}, ...children)
export function el(spec, attrs = {}, ...children) {
  const m = /^([a-z0-9]+)?((?:[.#][\w-]+)*)$/i.exec(spec);
  const node = document.createElement(m[1] || 'div');
  for (const part of (m[2] || '').match(/[.#][\w-]+/g) || []) {
    if (part[0] === '.') node.classList.add(part.slice(1)); else node.id = part.slice(1);
  }
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v === undefined || v === null || v === false) continue;
    if (k.startsWith('on') && typeof v === 'function') node.addEventListener(k.slice(2), v);
    else if (k === 'class') { for (const c of String(v).split(/\s+/)) if (c) node.classList.add(c); }
    else if (k === 'style' && typeof v === 'object') Object.assign(node.style, v);
    else if (k === 'dataset') Object.assign(node.dataset, v);
    else if (k in node && typeof v !== 'string') node[k] = v;
    else if (k === 'value' || k === 'checked') node[k] = v;
    else node.setAttribute(k, v === true ? '' : v);
  }
  for (const c of children.flat()) if (c !== null && c !== undefined && c !== false) node.append(c instanceof Node ? c : String(c));
  return node;
}

export function debounce(fn, ms) {
  let t = 0;
  const d = (...a) => { clearTimeout(t); t = setTimeout(() => fn(...a), ms); };
  d.flush = (...a) => { clearTimeout(t); fn(...a); };
  d.cancel = () => clearTimeout(t);
  return d;
}

// ---- preferences (localStorage can throw or be missing: private windows, blocked storage) ----
const PREFS_KEY = 'toms-atlas-web-prefs';
let prefsCache = null;
export function prefs() {
  if (prefsCache) return prefsCache;
  try { prefsCache = JSON.parse(localStorage.getItem(PREFS_KEY) || '{}') || {}; } catch { prefsCache = {}; }
  return prefsCache;
}
export function setPref(key, value) {
  prefs()[key] = value;
  try { localStorage.setItem(PREFS_KEY, JSON.stringify(prefsCache)); } catch { /* not persisted */ }
}

export function download(data, name, type = 'application/octet-stream') {
  const blob = data instanceof Blob ? data : new Blob([data], { type });
  const url = URL.createObjectURL(blob);
  const a = el('a', { href: url, download: name });
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 5000);
}

export const readBytes = (file) => file.arrayBuffer().then((b) => new Uint8Array(b));

export function baseName(p) { const s = p.replace(/\/+$/, ''); return s.slice(s.lastIndexOf('/') + 1); }
export function dirName(p) { const i = p.lastIndexOf('/'); return i <= 0 ? (i === 0 ? '/' : '.') : p.slice(0, i); }
export function stem(p) { const b = baseName(p); const i = b.lastIndexOf('.'); return i > 0 ? b.slice(0, i) : b; }
export function ext(p) { const b = baseName(p); const i = b.lastIndexOf('.'); return i > 0 ? b.slice(i + 1).toLowerCase() : ''; }

const collator = new Intl.Collator(undefined, { numeric: true, sensitivity: 'base' });
export const naturalCompare = (a, b) => collator.compare(a, b);

export const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
export const round3 = (v) => Math.round(v * 1000) / 1000;

// The format Export always writes, on top of the project's list: Cocos Creator reads sprite
// atlases only as a .plist.
export const ALWAYS_EXPORTED = 'plist';

// A modal prompt: resolves to the entered text, or null when cancelled.
export function promptDialog(title, value = '', { okText = 'OK', placeholder = '' } = {}) {
  return new Promise((resolve) => {
    const input = el('input', { type: 'text', value, placeholder, spellcheck: 'false' });
    const close = (v) => { back.remove(); resolve(v); };
    const form = el('form.dialog', { onsubmit: (e) => { e.preventDefault(); close(input.value.trim() || null); } },
      el('div.dialog-title', {}, title), input,
      el('div.dialog-buttons', {},
        el('button', { type: 'button', onclick: () => close(null) }, 'Cancel'),
        el('button.primary', { type: 'submit' }, okText)));
    const back = el('div.dialog-back', { onmousedown: (e) => { if (e.target === back) close(null); } }, form);
    form.addEventListener('keydown', (e) => { if (e.key === 'Escape') { e.stopPropagation(); close(null); } });
    document.body.append(back);
    input.focus();
    input.select();
  });
}

export function confirmDialog(title, text, okText = 'OK') {
  return new Promise((resolve) => {
    const close = (v) => { back.remove(); resolve(v); };
    const ok = el('button.primary', { type: 'button', onclick: () => close(true) }, okText);
    const form = el('div.dialog', { tabindex: '-1' },
      el('div.dialog-title', {}, title), text ? el('p', {}, text) : null,
      el('div.dialog-buttons', {},
        el('button', { type: 'button', onclick: () => close(false) }, 'Cancel'), ok));
    const back = el('div.dialog-back', { onmousedown: (e) => { if (e.target === back) close(false); } }, form);
    form.addEventListener('keydown', (e) => { if (e.key === 'Escape') { e.stopPropagation(); close(false); } });
    document.body.append(back);
    ok.focus();
  });
}

let toastTimer = 0;
export function toast(text, kind = 'info') {
  let t = $('#toast');
  if (!t) { t = el('div#toast'); document.body.append(t); }
  t.textContent = text;
  t.className = 'show ' + kind;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.className = ''; }, kind === 'error' ? 6000 : 2500);
}

// True when keyboard focus is in a text field (keyboard shortcuts leave it alone).
export function typingInField() {
  const a = document.activeElement;
  if (!a) return false;
  if (a.isContentEditable) return true;
  if (a.tagName === 'TEXTAREA' || a.tagName === 'SELECT') return true;
  return a.tagName === 'INPUT' && !['checkbox', 'radio', 'button', 'range', 'color', 'file'].includes(a.type);
}

// A dialog with several buttons and optional checkboxes: resolves to {button, checks: {key: bool}}
// (button null when cancelled). buttons: [{key, text, primary}], checks: [{key, text, checked}].
export function choiceDialog(title, text, buttons, checks = []) {
  return new Promise((resolve) => {
    const boxes = checks.map((c) => el('input', { type: 'checkbox', checked: !!c.checked, dataset: { key: c.key } }));
    const close = (button) => {
      back.remove();
      const st = {};
      checks.forEach((c, i) => { st[c.key] = boxes[i].checked; });
      resolve({ button, checks: st });
    };
    const btns = buttons.map((b) => el(b.primary ? 'button.primary' : 'button', { type: 'button', dataset: { key: b.key }, onclick: () => close(b.key) }, b.text));
    const form = el('div.dialog', { tabindex: '-1' },
      el('div.dialog-title', {}, title), text ? el('p', {}, text) : null,
      ...checks.map((c, i) => el('label.chk', {}, boxes[i], ' ', c.text)),
      el('div.dialog-buttons', {}, el('button', { type: 'button', onclick: () => close(null) }, 'Cancel'), ...btns));
    const back = el('div.dialog-back', { onmousedown: (e) => { if (e.target === back) close(null); } }, form);
    form.addEventListener('keydown', (e) => { if (e.key === 'Escape') { e.stopPropagation(); close(null); } });
    document.body.append(back);
    (btns.find((b, i) => buttons[i].primary) || btns[0])?.focus();
  });
}

// Opens the browser's file picker: resolves to [{rel, file}] (empty when cancelled).
// The input stays in the page as #pickInput while it is open (automated tests set its files).
export function pickFiles({ directory = false, multiple = true, accept = '' } = {}) {
  return new Promise((resolve) => {
    document.getElementById('pickInput')?.remove();
    const input = el('input#pickInput', { type: 'file', hidden: true, accept: accept || undefined });
    input.multiple = multiple || directory;
    if (directory) input.webkitdirectory = true;
    const done = (items) => { input.remove(); resolve(items); };
    input.addEventListener('change', () => done([...input.files].map((file) => ({ rel: file.webkitRelativePath || file.name, file }))));
    input.addEventListener('cancel', () => done([]));
    document.body.append(input);
    input.click();
  });
}

// A small context menu at (x, y): items [{text, action, disabled, danger}] or null for a separator.
export function contextMenu(x, y, items) {
  document.getElementById('ctxMenu')?.remove();
  const menu = el('div#ctxMenu', {}, ...items.map((it) => (it ? el('button', {
    type: 'button', disabled: !!it.disabled, class: it.danger ? 'danger' : '',
    onclick: () => { close(); it.action(); },
  }, it.text) : el('hr'))));
  const close = () => { menu.remove(); window.removeEventListener('mousedown', outside, true); window.removeEventListener('keydown', esc, true); };
  const outside = (e) => { if (!menu.contains(e.target)) close(); };
  const esc = (e) => { if (e.key === 'Escape') { e.stopPropagation(); close(); } };
  document.body.append(menu);
  const r = menu.getBoundingClientRect();
  menu.style.left = Math.max(0, Math.min(x, window.innerWidth - r.width - 4)) + 'px';
  menu.style.top = Math.max(0, Math.min(y, window.innerHeight - r.height - 4)) + 'px';
  setTimeout(() => { window.addEventListener('mousedown', outside, true); window.addEventListener('keydown', esc, true); });
  return menu;
}
