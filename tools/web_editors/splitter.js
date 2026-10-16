// splitter.js -- drag handles between the HTML editors' panels (event and stage editor).
//
//   addSplitter({ key, target, axis, after, min, max, onChange })
//     target    the panel whose size the handle changes
//     axis      'x' (a width: a handle on the panel's side) or 'y' (a height: a handle above or below it)
//     after     false: the handle goes right after the panel (drag right / down = bigger);
//               true: right before it (drag left / up = bigger, e.g. a dock on the right)
//     min, max  limits in pixels; max may be a function (read when a drag starts)
//     onChange(final)  called while dragging (final = false) and once at the end (true), to redraw what depends on it
//
// Each size is kept per browser (localStorage "toms-split-<key>"); a double-click on a handle puts the panel back to
// the size the page gives it. Nothing is written into the project.
'use strict';
(function () {
  const store = {
    get(k) { try { const v = localStorage.getItem('toms-split-' + k); return v == null ? null : +v; } catch (_) { return null; } },
    set(k, v) { try { localStorage.setItem('toms-split-' + k, String(Math.round(v))); } catch (_) {} },
    del(k) { try { localStorage.removeItem('toms-split-' + k); } catch (_) {} },
  };
  const style = document.createElement('style');
  style.textContent = `
    .split { flex: none; background: transparent; position: relative; z-index: 3; user-select: none; touch-action: none; }
    .split.x { width: 5px; cursor: col-resize; margin: 0 -2px; }
    .split.y { height: 5px; cursor: row-resize; margin: -2px 0; }
    .split:hover, .split.drag { background: var(--accent, #4c9eff); opacity: .55; }
    body.splitting.x, body.splitting.x * { cursor: col-resize !important; user-select: none !important; }
    body.splitting.y, body.splitting.y * { cursor: row-resize !important; user-select: none !important; }`;
  document.head.appendChild(style);

  function apply(target, axis, px, orig) {
    const prop = axis === 'x' ? 'width' : 'height';
    if (px == null) { target.style[prop] = orig.size; target.style.flex = orig.flex; return; }   // back to the page's own size
    target.style.flex = `0 0 ${px}px`;   // works for flex children (the docks, the event editor's top half) and blocks
    target.style[prop] = px + 'px';
  }

  window.addSplitter = function ({ key, target, axis = 'x', after = false, min = 120, max = 2000, onChange }) {
    if (!target) return null;
    const orig = { size: target.style[axis === 'x' ? 'width' : 'height'], flex: target.style.flex };
    const maxNow = () => Math.max(min, typeof max === 'function' ? max() : max);
    const h = document.createElement('div');
    h.className = 'split ' + axis;
    target.parentNode.insertBefore(h, after ? target : target.nextSibling);
    const saved = store.get(key);
    if (saved) apply(target, axis, Math.max(min, Math.min(maxNow(), saved)), orig);
    let start = 0, size0 = 0, hi = 0, raf = 0;
    // the stage editor zooms its whole page (body { zoom }): sizes are kept in CSS pixels, the mouse moves in screen pixels
    const zoom = () => parseFloat(getComputedStyle(document.body).zoom) || 1;
    const sizeOf = () => { const r = target.getBoundingClientRect(); return (axis === 'x' ? r.width : r.height) / zoom(); };
    const move = ev => {
      const d = ((axis === 'x' ? ev.clientX : ev.clientY) - start) / zoom();
      apply(target, axis, Math.max(min, Math.min(hi, size0 + (after ? -d : d))), orig);
      if (onChange && !raf) raf = requestAnimationFrame(() => { raf = 0; onChange(false); });
    };
    const up = () => {
      removeEventListener('pointermove', move); removeEventListener('pointerup', up);
      h.classList.remove('drag'); document.body.classList.remove('splitting', axis);
      store.set(key, sizeOf()); if (onChange) onChange(true);
    };
    h.addEventListener('pointerdown', ev => {
      ev.preventDefault(); start = axis === 'x' ? ev.clientX : ev.clientY; size0 = sizeOf(); hi = Math.max(maxNow(), size0);   // never jumps when the drag starts
      h.classList.add('drag'); document.body.classList.add('splitting', axis);
      addEventListener('pointermove', move); addEventListener('pointerup', up);
    });
    h.addEventListener('dblclick', () => { apply(target, axis, null, orig); store.del(key); if (onChange) onChange(true); });
    return h;
  };
})();
