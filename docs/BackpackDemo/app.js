const DATA_URL = 'item.json';

const sampleState = {
  player: {
    name: '王者冒險者',
    level: 12,
    hp: 480,
    hpMax: 640,
    mp: 120,
    mpMax: 220,
    str: 88,
    def: 52,
    agi: 36,
    exp: 1320,
    gold: 240,
    weight: 18,
    capacity: 48,
  },
  items: {
    iron_sword: 1,
    healing_potion: 3,
    giant_tonic: 1,
    guardian_shield: 1,
    feather_boots: 1,
    phoenix_scroll: 1,
    moon_ring: 1,
    adventurer_map: 1,
  }
};

const state = {
  items: [],
  selected: 0,
  menuOpen: false,
  live: structuredClone(sampleState),
};

const el = (id) => document.getElementById(id);
const fmt = (n) => `${n}`.replace(/\B(?=(\d{3})+(?!\d))/g, ',');

function clone(obj){ return JSON.parse(JSON.stringify(obj)); }

async function loadData(){
  const res = await fetch(DATA_URL, {cache:'no-store'});
  const json = await res.json();
  return Object.values(json);
}

function effectToText(effect={}){
  const parts = [];
  for(const [k,v] of Object.entries(effect)){
    const sign = v >= 0 ? '+' : '';
    const label = ({str:'STR',def:'DEF',agi:'AGI',hp:'HP',mp:'MP',exp:'EXP',gold:'Gold'})[k] || k.toUpperCase();
    parts.push(`${label}${sign}${v}`);
  }
  return parts.length ? parts : ['無'];
}

function renderStats(){
  const p = state.live.player;
  const rows = [
    ['Name', p.name],
    ['Level', p.level],
    ['HP', `${fmt(p.hp)} / ${fmt(p.hpMax)}`],
    ['MP', `${fmt(p.mp)} / ${fmt(p.mpMax)}`],
    ['STR', p.str],
    ['DEF', p.def],
    ['AGI', p.agi],
    ['EXP', fmt(p.exp)],
    ['Gold', fmt(p.gold)],
    ['Weight', `${p.weight} / ${p.capacity}`],
  ];
  el('player-stats').innerHTML = rows.map(([k,v]) => `
    <div class="stat-row"><small>${k}</small><strong>${v}</strong></div>
  `).join('');
}

function getInventoryList(){
  const entries = state.items.map(item => ({...item, count: state.live.items[item.gameId] || 0})).filter(x => x.count > 0);
  return entries;
}

function renderInventory(){
  const items = getInventoryList();
  const grid = el('inventory-grid');
  const tpl = el('item-template');
  grid.innerHTML = '';
  items.forEach((item, idx) => {
    const node = tpl.content.firstElementChild.cloneNode(true);
    node.dataset.index = idx;
    node.setAttribute('aria-selected', idx === state.selected ? 'true' : 'false');
    node.querySelector('.item-name').textContent = item.name;
    node.querySelector('.item-meta').textContent = item.desc || effectToText(item.effect).join(' • ');
    node.querySelector('.item-count').textContent = `×${item.count}`;
    const icon = node.querySelector('.item-icon');
    icon.src = item.icon;
    icon.alt = item.name;
    node.addEventListener('click', () => selectItem(idx, true));
    node.addEventListener('keydown', (e) => {
      if (e.key === 'Enter' || e.key === ' ') {
        e.preventDefault();
        selectItem(idx, true);
      }
    });
    node.tabIndex = 0;
    grid.appendChild(node);
  });
  if (state.selected >= items.length) state.selected = Math.max(0, items.length - 1);
  if (items.length) updateDetail(items[state.selected]);
  else showEmpty();
}

function showEmpty(){
  el('detail').classList.add('hidden');
  el('detail-empty').classList.remove('hidden');
}

function updateDetail(item){
  if(!item){ showEmpty(); return; }
  el('detail-empty').classList.add('hidden');
  el('detail').classList.remove('hidden');
  el('detail-icon').src = item.icon;
  el('detail-icon').alt = item.name;
  el('detail-name').textContent = item.name;
  el('detail-id').textContent = `gameId: ${item.gameId}  |  icon: ${item.icon}`;
  const chips = [item.type || '道具', item.stackable ? '可堆疊' : '非堆疊', `數量 ×${state.live.items[item.gameId] || 0}`];
  el('detail-chips').innerHTML = chips.map(t => `<span class="chip">${t}</span>`).join('');
  el('detail-desc').textContent = item.description || item.desc || '—';
  el('detail-effect').innerHTML = effectToText(item.effect).map(t => `<span class="effect-pill">${t}</span>`).join('');
}

function selectItem(idx, openMenu=false){
  state.selected = idx;
  renderInventory();
  const item = getInventoryList()[idx];
  if (item) updateDetail(item);
  if (openMenu) openActionSheet(item);
}

function openActionSheet(item){
  if(!item) return;
  state.menuOpen = true;
  el('menu-overlay').classList.remove('hidden');
  el('action-sheet').classList.remove('hidden');
  el('sheet-icon').src = item.icon;
  el('sheet-name').textContent = item.name;
  el('sheet-summary').textContent = `${item.gameId} · ${item.category || 'RPG Item'} · 數量 ×${state.live.items[item.gameId] || 0}`;
  el('sheet-effect').innerHTML = effectToText(item.effect).map(t => `<span class="effect-pill">${t}</span>`).join(' ');
  el('btn-use').focus();
}

function closeActionSheet(){
  state.menuOpen = false;
  el('menu-overlay').classList.add('hidden');
  el('action-sheet').classList.add('hidden');
  document.querySelectorAll('.item-card')[state.selected]?.focus();
}

function applyEffect(item){
  const p = state.live.player;
  const eff = item.effect || {};
  for (const [k, v] of Object.entries(eff)) {
    if (k === 'hp') p.hp = Math.max(0, Math.min(p.hpMax, p.hp + v));
    else if (k === 'mp') p.mp = Math.max(0, Math.min(p.mpMax, p.mp + v));
    else if (k === 'gold') p.gold += v;
    else if (k === 'exp') p.exp += v;
    else if (typeof p[k] === 'number') p[k] += v;
  }
}

function consumeSelected(){
  const item = getInventoryList()[state.selected];
  if(!item) return;
  applyEffect(item);
  if(item.stackable){
    state.live.items[item.gameId] = Math.max(0, (state.live.items[item.gameId] || 0) - 1);
  } else {
    state.live.items[item.gameId] = 0;
  }
  renderAll();
  closeActionSheet();
}

function dropSelected(){
  const item = getInventoryList()[state.selected];
  if(!item) return;
  state.live.items[item.gameId] = 0;
  renderAll();
  closeActionSheet();
}

function renderAll(){
  renderStats();
  renderInventory();
}

function resetDemo(){
  state.live = clone(sampleState);
  state.selected = 0;
  renderAll();
}

function bindEvents(){
  el('btn-open').addEventListener('click', () => {
    const item = getInventoryList()[state.selected];
    if (item) openActionSheet(item);
  });
  el('btn-reset').addEventListener('click', resetDemo);
  el('btn-prev').addEventListener('click', () => {
    const items = getInventoryList();
    if (!items.length) return;
    state.selected = (state.selected - 1 + items.length) % items.length;
    renderInventory();
    updateDetail(items[state.selected]);
  });
  el('btn-next').addEventListener('click', () => {
    const items = getInventoryList();
    if (!items.length) return;
    state.selected = (state.selected + 1) % items.length;
    renderInventory();
    updateDetail(items[state.selected]);
  });
  el('btn-use').addEventListener('click', consumeSelected);
  el('btn-drop').addEventListener('click', dropSelected);
  el('btn-close').addEventListener('click', closeActionSheet);
  el('menu-overlay').addEventListener('click', closeActionSheet);
  window.addEventListener('keydown', (e) => {
    if (state.menuOpen && e.key === 'Escape') { closeActionSheet(); return; }
    const items = getInventoryList();
    if (!items.length) return;
    if (e.key === 'ArrowLeft' || e.key === 'ArrowUp') {
      e.preventDefault();
      state.selected = (state.selected - 1 + items.length) % items.length;
      renderInventory();
      updateDetail(items[state.selected]);
    } else if (e.key === 'ArrowRight' || e.key === 'ArrowDown') {
      e.preventDefault();
      state.selected = (state.selected + 1) % items.length;
      renderInventory();
      updateDetail(items[state.selected]);
    } else if (e.key === 'Enter' || e.key === ' ') {
      e.preventDefault();
      openActionSheet(items[state.selected]);
    } else if (e.key.toLowerCase() === 'u') {
      e.preventDefault();
      openActionSheet(items[state.selected]);
      consumeSelected();
    } else if (e.key.toLowerCase() === 'd') {
      e.preventDefault();
      openActionSheet(items[state.selected]);
      dropSelected();
    }
  }, {passive:false});
}

async function init(){
  state.items = await loadData();
  bindEvents();
  renderAll();
  const items = getInventoryList();
  if (items[0]) updateDetail(items[0]);
}

init().catch(err => {
  console.error(err);
  document.body.innerHTML = `<pre style="color:#fff;background:#111;padding:20px;white-space:pre-wrap">${err.stack || err}</pre>`;
});
