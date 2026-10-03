// sprites.js -- the Sprites panel: folder tree (by '/'), child sprites under their parent,
// thumbnails, filter, multi-select, Delete, double-click = sprite edit mode, right-click = menu
// (Replace image, Save image as, Rename, Delete).
import { $, el, prefs, setPref, naturalCompare, debounce } from './util.js';

const THUMB = 22;

export class SpritesPanel {
  constructor(app, root) {
    this.app = app;
    this.root = root;
    this.collapsed = new Set(prefs().collapsed || []);
    this.filter = '';
    this.rows = [];        // visible sprite names in order (shift-click ranges)
    this.anchor = null;
    this.list = $('#spriteTree', root);
    this.count = $('#spriteCount', root);
    const filter = $('#spriteFilter', root);
    filter.value = prefs().filter || '';
    this.filter = filter.value;
    filter.addEventListener('input', debounce(() => { this.filter = filter.value; setPref('filter', this.filter); this.render(); }, 80));
    $('#deleteBtn', root).onclick = () => app.deleteSelected();
    $('#collapseBtn', root).onclick = () => {
      const all = this.allFolderKeys();
      const anyOpen = all.some((k) => !this.collapsed.has(k));
      this.collapsed = anyOpen ? new Set(all) : new Set();
      this.saveCollapsed();
      this.render();
    };
    this.list.addEventListener('keydown', (e) => this.onKey(e));
    app.on('build', () => this.render());
    app.on('selection', () => this.renderSelection(true));
  }

  saveCollapsed() { setPref('collapsed', [...this.collapsed].slice(0, 2000)); }

  // folder tree: {folders: Map(name -> node), sprites: [{s, kids: []}]}
  tree() {
    const root = { key: '', folders: new Map(), sprites: [] };
    const byName = new Map();
    const sprites = [...this.app.resolved].sort((a, b) => naturalCompare(a.name, b.name));
    for (const s of sprites) byName.set(s.name, { s, kids: [] });
    for (const s of sprites) {
      const node = byName.get(s.name);
      if (s.child && byName.has(s.parent)) { byName.get(s.parent).kids.push(node); continue; }
      const parts = s.name.split('/');
      let f = root;
      for (let i = 0; i < parts.length - 1; i++) {
        const key = parts.slice(0, i + 1).join('/') + '/';
        if (!f.folders.has(parts[i])) f.folders.set(parts[i], { key, name: parts[i], folders: new Map(), sprites: [] });
        f = f.folders.get(parts[i]);
      }
      f.sprites.push(node);
    }
    return root;
  }

  allFolderKeys() {
    const keys = [];
    const walk = (f) => { for (const sub of f.folders.values()) { keys.push(sub.key); walk(sub); } };
    walk(this.tree());
    for (const s of this.app.resolved) if (this.app.resolved.some((c) => c.parent === s.name)) keys.push('@' + s.name);
    return keys;
  }

  matches(node) {
    if (!this.filter) return true;
    const f = this.filter.toLowerCase();
    if (node.s.name.toLowerCase().includes(f)) return true;
    if ((this.app.entry(node.s.name)?.tags || []).some((t) => t.toLowerCase().includes(f))) return true;
    return node.kids.some((k) => this.matches(k));
  }
  folderMatches(f) {
    return f.sprites.some((n) => this.matches(n)) || [...f.folders.values()].some((x) => this.folderMatches(x));
  }

  render() {
    const app = this.app;
    const problems = new Map();
    for (const d of app.result?.diagnostics || []) if (d.sprite && d.level !== 'info') {
      if (problems.get(d.sprite) !== 'error') problems.set(d.sprite, d.level);
    }
    const out = [];
    this.rows = [];
    const filtering = !!this.filter;
    const spriteRow = (node, depth) => {
      if (!this.matches(node)) return;
      const s = node.s;
      const key = '@' + s.name;
      const open = filtering || !this.collapsed.has(key);
      const r = app.regions.get(s.name);
      const entry = app.entry(s.name);
      const leaf = s.child ? s.name : s.name.slice(s.name.lastIndexOf('/') + 1);
      this.rows.push(s.name);
      out.push(el('li.row.sprite', {
        dataset: { name: s.name }, title: s.name + (s.file ? '\n' + s.file : '') + (s.child ? '\nchild of ' + s.parent : ''),
        class: [s.exclude ? 'excluded' : '', s.child ? 'child' : '', problems.has(s.name) ? 'p-' + problems.get(s.name) : ''].join(' '),
        style: { paddingLeft: 6 + depth * 14 + 'px' },
      },
      node.kids.length ? el('span.tog', { dataset: { key }, class: open ? 'open' : '' }) : el('span.tog.none'),
      this.thumb(r),
      el('span.label', {}, leaf),
      entry?.pin ? el('span.badge.pin', { title: `pinned: page ${entry.pin[0]} at ${entry.pin[1]},${entry.pin[2]}` }, 'pin') : null,
      s.child ? el('span.badge', { title: entry?.bake ? 'child sprite, baked (own pixels)' : 'child sprite (rect in its parent)' }, entry?.bake ? 'bake' : 'child') : null,
      r && r.aliasOf ? el('span.badge', { title: 'same pixels as ' + r.aliasOf }, '=') : null,
      s.exclude ? el('span.badge', {}, 'off') : null,
      problems.has(s.name) ? el('span.badge.warn', {}, problems.get(s.name) === 'error' ? '!' : '?') : null));
      if (open) for (const k of node.kids) spriteRow(k, depth + 1);
    };
    const folderRows = (f, depth) => {
      for (const sub of [...f.folders.values()].sort((a, b) => naturalCompare(a.name, b.name))) {
        if (!this.folderMatches(sub)) continue;
        const open = filtering || !this.collapsed.has(sub.key);
        out.push(el('li.row.folder', { dataset: { key: sub.key }, style: { paddingLeft: 6 + depth * 14 + 'px' }, title: sub.key },
          el('span.tog', { dataset: { key: sub.key }, class: open ? 'open' : '' }),
          el('span.folder-icon'), el('span.label', {}, sub.name)));
        if (open) folderRows(sub, depth + 1);
      }
      for (const n of f.sprites) spriteRow(n, depth);
    };
    folderRows(this.tree(), 0);
    if (!out.length) {
      out.push(el('li.empty', {}, app.resolved.length ? 'Nothing matches the filter.'
        : 'No sprites yet. Drop PNG files or a folder anywhere, or use + Files / + Folder.'));
    }
    const scroll = this.list.scrollTop;
    this.list.replaceChildren(...out);
    this.list.scrollTop = scroll;
    this.count.textContent = String(app.resolved.length);
    for (const row of this.list.children) this.bindRow(row);
    this.renderSelection(false);
  }

  thumb(r) {
    const c = el('canvas.thumb', { width: THUMB, height: THUMB });
    const page = r && this.app.pages[r.page];
    if (!page) return c;
    const [x, y, w, h] = r.frame;
    if (w <= 0 || h <= 0) return c;
    const ctx = c.getContext('2d');
    const s = Math.min(THUMB / w, THUMB / h, 4);
    ctx.imageSmoothingEnabled = s < 1;
    ctx.drawImage(page, x, y, w, h, (THUMB - w * s) / 2, (THUMB - h * s) / 2, w * s, h * s);
    return c;
  }

  bindRow(row) {
    if (!row.classList.contains('row')) return;
    row.addEventListener('mousedown', (e) => {
      const tog = e.target.closest('.tog');
      if (tog && tog.dataset.key) { this.toggle(tog.dataset.key); e.preventDefault(); return; }
      if (row.classList.contains('folder')) {
        if (e.detail === 1) this.toggle(row.dataset.key);
        return;
      }
      const name = row.dataset.name;
      if (e.shiftKey && this.anchor && this.rows.includes(this.anchor)) {
        const a = this.rows.indexOf(this.anchor), b = this.rows.indexOf(name);
        const range = this.rows.slice(Math.min(a, b), Math.max(a, b) + 1);
        if (b < a) range.reverse();
        this.app.select(range, { add: e.ctrlKey || e.metaKey });
      } else if (e.ctrlKey || e.metaKey) {
        this.app.select([name], { toggle: true });
        this.anchor = name;
      } else {
        this.app.select([name]);
        this.anchor = name;
      }
      this.list.focus({ preventScroll: true });
    });
    row.addEventListener('dblclick', () => { if (row.dataset.name) this.app.setEdit(row.dataset.name); });
    row.addEventListener('contextmenu', (e) => {
      if (!row.dataset.name) return;
      e.preventDefault();
      this.app.spriteMenu(row.dataset.name, e.clientX, e.clientY);
    });
  }

  toggle(key) {
    if (this.collapsed.has(key)) this.collapsed.delete(key); else this.collapsed.add(key);
    this.saveCollapsed();
    this.render();
  }

  renderSelection(scroll) {
    const sel = new Set(this.app.selection);
    let cur = null;
    for (const row of this.list.querySelectorAll('.row.sprite')) {
      const on = sel.has(row.dataset.name);
      row.classList.toggle('selected', on);
      row.classList.toggle('current', row.dataset.name === this.app.current);
      if (row.dataset.name === this.app.current) cur = row;
    }
    if (scroll && cur) cur.scrollIntoView({ block: 'nearest' });
  }

  onKey(e) {
    if (e.key !== 'ArrowDown' && e.key !== 'ArrowUp') return;
    e.preventDefault();
    const i = this.rows.indexOf(this.app.current);
    const j = Math.max(0, Math.min(this.rows.length - 1, i < 0 ? 0 : i + (e.key === 'ArrowDown' ? 1 : -1)));
    if (this.rows[j]) { this.app.select([this.rows[j]], { add: e.shiftKey }); this.anchor = this.rows[j]; }
  }
}
