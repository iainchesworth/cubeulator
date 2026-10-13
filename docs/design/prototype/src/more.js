// ---- shared bits for the plainer screens ---------------------------------------
// Language names are data, not strings: each is written in its own language and never translated.
const LANGS = {en: 'English', de: 'Deutsch', fr: 'Français', es: 'Español', ja: '日本語', zh: '简体中文', he: 'עברית', ar: 'العربية'};
const row = (label, act, arg, o = {}) => `<button type="button" class="row" data-act="${act}"${arg ? ` data-arg="${arg}"` : ''}>${o.lead || ''}<span class="grow">${label}${o.sub ? `<br><span class="note">${o.sub}</span>` : ''}</span>${o.val ? `<span class="val">${o.val}</span>` : ''}${ico('chev-r', 'flip')}</button>`;
const sw = (label, note, on, key) => `<button type="button" class="row" role="switch" aria-checked="${!!on}" data-act="tog" data-arg="${key}"><span class="grow">${label}${note ? `<br><span class="note">${note}</span>` : ''}</span><span class="switch"><i>${ico('check')}</i></span></button>`;
const choice = (label, items, cur, key) => `<div class="setting"><span>${label}</span><div class="choice" role="radiogroup" aria-label="${label}">${items.map(([v, l]) => `<button type="button" class="btn" role="radio" aria-checked="${v === cur}" data-act="opt" data-arg="${key}=${v}">${v === cur ? ico('check') : ''}<span>${l}</span></button>`).join('')}</div></div>`;
const notice = (kind, title, body, acts) => `<div class="notice ${kind}" role="status">${ico(kind === 'ok' ? 'check' : kind === 'info' ? 'info' : 'alert')}<div><strong>${title}</strong>${body ? `<span>${body}</span>` : ''}${acts ? `<div class="acts">${acts}</div>` : ''}</div></div>`;
const cubeAside = () => `<div class="aside"><canvas data-scene="home" aria-hidden="true"></canvas></div>`;
const prose = text => text.split('<br><br>').map(p => `<p>${p}</p>`).join('');

// ---- first run ---------------------------------------------------------------------
screens['first-run'] = {
  render(st) {
    const rev = st === 'revisit';
    const steps = [1, 2, 3].map(i => `<li><span class="n mono">${i}</span><div><strong>${t('firstRun.' + 'step' + i + '.title')}</strong><br><span class="t-sec">${t('firstRun.' + 'step' + i + '.body')}</span></div></li>`).join('');
    const lang = `<button type="button" class="btn ghost" data-act="pick" data-arg="settings.language" aria-label="${t('firstRun.language')}"><span>${LANGS[P.d.lang] || LANGS.en}</span>${ico('chev-r', 'flip')}</button>`;
    return `<div class="screen">${rev ? topbar(t('help.howItWorks')) : ''}<div class="split"><div class="pane page" data-scroll="fr"><div class="col fill">
      ${rev ? '' : `<div class="between"><span class="eyebrow">${t('app.name')}</span>${lang}</div>`}
      <h1 class="t-title">${t('firstRun.title')}</h1><ol class="steps">${steps}</ol>
      <div><strong>${t('firstRun.need.title')}</strong><ul class="plain"><li>${t('firstRun.need.cube')}</li><li>${t('firstRun.need.light')}</li></ul></div>
      <div class="push">${btn(t(rev ? 'common.done' : 'firstRun.start'), rev ? 'back' : 'firstRunDone', {v: 'primary', cls: 'block thumb', trail: rev ? '' : 'chev-r'})}</div></div></div>${cubeAside()}</div></div>`;
  }
};

// ---- permission ----------------------------------------------------------------------
screens.permission = {
  enter(st) { if (st === 'default') return P.d.permRefused ? 'refused' : 'ask'; if (st === 'refused') P.d.permRefused = true; if (st === 'ask') P.d.permRefused = false; },
  render(st) {
    const refused = P.d.permRefused && st !== 'ask';
    const points = [1, 2, 3].map(i => `<li>${ico('check')}<span>${t('permission.' + 'point' + i)}</span></li>`).join('');
    // The device draws its own prompt. This stands in for it; its wording is the system's, apart from the reason.
    const sys = st === 'system' ? `<div class="scrim"><div class="dialog sys" role="alertdialog" aria-modal="true" aria-label="System prompt"><p class="tagline">drawn by the device, not by the app</p><h2 class="t-headline">“Cubeulator” would like to use the camera</h2><p>${t('permission.system.camera')}</p><div class="acts">${btn('Don’t allow', 'permDeny')}${btn('Allow', 'permAllow')}</div></div></div>` : '';
    const acts = refused
      ? btn(t('common.openDeviceSettings'), 'permAllow', {v: 'primary', cls: 'block thumb'}) + btn(t('permission.askAgain'), 'pick', {cls: 'block', arg: 'permission.system'}) + btn(t('common.enterByHand'), 'go', {cls: 'block', arg: 'review'}) + btn(t('permission.notNow'), 'back', {v: 'ghost'})
      : btn(t('permission.allow'), 'pick', {v: 'primary', cls: 'block thumb', arg: 'permission.system'}) + btn(t('common.enterByHand'), 'go', {cls: 'block', arg: 'review'}) + btn(t('permission.notNow'), 'back', {v: 'ghost'});
    return `<div class="screen">${topbar('')}<div class="split"><div class="pane page" data-scroll="perm"><div class="col fill">
      <div class="iconbox">${ico(refused ? 'alert' : 'camera')}</div>
      <h1 class="t-title">${t(refused ? 'permission.refused.title' : 'permission.title')}</h1><p>${t(refused ? 'permission.refused.body' : 'permission.body')}</p>
      ${refused ? '' : `<ul class="points">${points}</ul>`}<div class="home-actions push" style="flex-direction:column">${acts}</div></div></div>${cubeAside()}</div>${sys}</div>`;
  }
};

// ---- review and hand entry --------------------------------------------------------------
const RV = {mode: 'enter', f: null, side: 'U', colour: 'white', dirty: false, centre: false};
const RORDER = ['U', 'F', 'R', 'B', 'L', 'D'];
const NETPOS = {U: [4, 1], L: [1, 4], F: [4, 4], R: [7, 4], B: [10, 4], D: [4, 7]};
function rvFill(sides) {
  RV.f = {};
  for (const s of RORDER) { const full = sideColours(SOL.start, s); RV.f[s] = sides.includes(s) ? full.slice() : full.map((c, i) => i === 4 ? c : null); }
  RV.dirty = false; RV.centre = false; RV.side = RORDER.find(s => RV.f[s].some(c => !c)) || 'U';
}
function rvCount() {
  const n = {}; let unset = 0;
  for (const s of RORDER) for (const c of RV.f[s]) { if (!c) unset++; else n[c] = (n[c] || 0) + 1; }
  return {n, unset};
}
function reviewState() {
  const {n, unset} = rvCount();
  if (unset === 48) return 'empty';
  if (unset) return 'partial';
  if (Object.keys(MARKS).some(c => (n[c] || 0) !== 9)) return 'miscount';
  // The prototype knows one real cube. Anything else with nine of each stands in for "cannot be a real cube".
  return RORDER.every(s => sideColours(SOL.start, s).join() === RV.f[s].join()) ? 'valid' : 'impossible';
}
screens.review = {
  enter(st, o) {
    const from = (P.stack[P.stack.length - 1] || {}).screen;
    if (st === 'default') {
      if (from === 'scan') { RV.mode = 'check'; rvFill(sim.sides.length ? sim.sides : []); if (sim.phase === 'conflict') { rvFill(RORDER); rvBreak('miscount'); } if (!sim.sides.length) RV.mode = 'enter'; }
      else if (from === 'solve') { RV.mode = 'check'; rvFill(RORDER); }
      else { RV.mode = 'enter'; rvFill([]); }
      return reviewState();
    }
    if (!o.picked && RV.f) return;
    RV.mode = st === 'empty' || st === 'partial' ? 'enter' : 'check';
    rvFill(st === 'empty' ? [] : st === 'partial' ? ['U', 'F'] : RORDER);
    if (st === 'miscount' || st === 'impossible') rvBreak(st);
    if (st === 'confirm-discard') RV.dirty = true;
  },
  render(st) {
    const base = reviewState(), {n, unset} = rvCount(), nm = s => t('colourSide.' + FACE_COL[s]);
    const sides = RORDER.map(s => {
      const left = RV.f[s].filter(c => !c).length;
      return `<button type="button" class="sidebtn" data-act="rvSide" data-arg="${s}" aria-pressed="${s === RV.side}" aria-label="${left ? t('review.a11y.sideLeft', left, nm(s)) : t('review.a11y.sideDone', nm(s))}">${stk(FACE_COL[s])}${left ? '' : `<span class="tick">${ico('check')}</span>`}</button>`;
    }).join('');
    const cells = RV.f[RV.side].map((c, i) => `<button type="button" class="cell${c ? '' : ' unset'}${i === 4 ? ' centre' : ''}" data-act="rvPaint" data-arg="${i}" aria-label="${t('review.a11y.sticker', nm(RV.side), Math.floor(i / 3) + 1, i % 3 + 1, t(c ? 'colour.' + c : 'colour.unset'))}">${stk(c)}</button>`).join('');
    const pal = Object.keys(MARKS).map((c, i) => `<button type="button" class="pal" data-act="rvColour" data-arg="${c}" aria-pressed="${c === RV.colour}" aria-label="${t('colour.' + c)}">${stk(c)}${c === RV.colour ? `<span class="tick">${ico('check')}</span>` : ''}</button>`).join('');
    const net = RORDER.map(s => RV.f[s].map((c, i) => `<span class="${c ? '' : 'unset'}${s === RV.side ? ' sel' : ''}" style="grid-column:${NETPOS[s][0] + i % 3};grid-row:${NETPOS[s][1] + Math.floor(i / 3)}">${c ? stk(c) : ''}</span>`).join('')).join('');
    const list = pred => Object.keys(MARKS).filter(c => pred(n[c] || 0)).map(c => t('review.status.countItem', t('colour.' + c), n[c] || 0)).join(', ');
    let status;
    if (st === 'solve-failed') status = notice('caution', t('review.failed.title'), t('review.failed.body'), btn(t('common.tryAgain'), 'rvSolve') + btn(t('common.scanInstead'), 'scan'));
    else if (RV.centre) status = notice('info', t('review.centre'), '');
    else if (base === 'empty') status = notice('info', t('review.status.empty'), '');
    else if (base === 'partial') status = notice('info', t('review.status.partial', unset), '');
    else if (base === 'miscount') status = notice('caution', t('review.status.miscount'), [list(x => x > 9) && t('review.status.miscountOver', list(x => x > 9)), list(x => x < 9) && t('review.status.miscountUnder', list(x => x < 9))].filter(Boolean).join('. '));
    else if (base === 'impossible') status = notice('caution', t('review.status.impossible'), t('review.status.impossibleBody'));
    else status = notice('ok', t('review.status.valid'), '');
    const layer = st === 'confirm-discard' ? dialog(t('review.discard.title'), t('review.discard.body'), btn(t('review.discard.keep'), 'closeSheet'), btn(t('review.discard.confirm'), 'rvDiscard', {v: 'ghost'})) : '';
    if (st === 'working') return `<div class="screen">${working(t('review.working'))}</div>`;
    const help = `<button type="button" class="iconbtn" data-act="go" data-arg="help.entering" aria-label="${t('common.help')}">${ico('help')}</button>`;
    return `<div class="screen">${topbar(t(RV.mode === 'check' ? 'review.title.check' : 'review.title.enter'), help)}<div class="review" data-scroll="rv">
      <p class="rv-intro t-small t-sec">${t(RV.mode === 'check' ? 'review.intro.check' : 'review.intro.enter')}</p>
      <div class="sides" role="group" aria-label="${t('review.sidePicker')}">${sides}</div>
      <div class="bigwrap"><div class="bigside" role="group" aria-label="${nm(RV.side)}">${cells}</div></div>
      <div class="rv-pal"><div class="palette" role="group" aria-label="${t('review.palette')}">${pal}</div>
        <div class="between"><strong>${t('colour.' + RV.colour)}</strong>${btn(t('review.clearSide'), 'rvClear', {v: 'ghost'})}</div></div>
      <div class="rv-net" aria-hidden="true">${net}</div>
      <div class="rv-status">${status}</div>
      <div class="rv-acts">${btn(t('common.scanInstead'), 'rvScan', {v: 'ghost', cls: 'thumb'})}${btn(t('review.solve'), 'rvSolve', {v: 'primary', cls: 'thumb', trail: 'chev-r', dis: base !== 'valid'})}</div></div>${layer}</div>`;
  }
};
function rvBreak(kind) {
  const f = RV.f.F, a = f.findIndex((c, i) => i !== 4), b = f.findIndex((c, i) => i !== 4 && c !== f[a]);
  if (kind === 'miscount') f[a] = f[a] === 'red' ? 'blue' : 'red';
  else { const x = f[a]; f[a] = f[b]; f[b] = x; }
  RV.side = 'F';
}
Object.assign(actions, {
  rvSide(el) { RV.side = el.dataset.arg; RV.centre = false; render(); },
  rvColour(el) { RV.colour = el.dataset.arg; RV.centre = false; render(); },
  rvPaint(el) {
    const i = +el.dataset.arg; RV.centre = i === 4;
    if (i !== 4) {
      const was = RV.f[RV.side].every(Boolean);
      RV.f[RV.side][i] = RV.colour; RV.dirty = true;
      if (!was && RV.f[RV.side].every(Boolean)) RV.side = RORDER.find(s => RV.f[s].some(c => !c)) || RV.side;
    }
    P.state = reviewState(); render();
  },
  rvClear() { RV.f[RV.side] = RV.f[RV.side].map((c, i) => i === 4 ? c : null); RV.dirty = true; P.state = reviewState(); render(); },
  rvSolve() { P.state = 'working'; render(); setTimeout(() => { if (P.screen !== 'review' || P.state !== 'working') return; RV.f = null; solveReset(0); P.d.t0 = Date.now(); go('solve', 'hold', {push: false}); }, 1300); },
  rvScan() { if (RV.dirty) { RV.then = 'scan'; P.state = 'confirm-discard'; return render(); } RV.f = null; actions.scan(); },
  rvDiscard() { const then = RV.then; RV.f = null; RV.dirty = false; RV.then = null; if (then === 'scan') return actions.scan(); popBack(); }
});

// ---- the solution ------------------------------------------------------------------------
const SV = {playing: false, timer: 0};
const solState = () => SV.playing ? 'playing' : S.turn >= N ? 'end' : S.turn === 0 ? 'overview' : 'stepping';
function solStop() { SV.playing = false; clearInterval(SV.timer); }
function solStep(dir) {
  if (dir > 0 && S.turn >= N) return solStop();
  if (dir < 0 && S.turn <= 0) return;
  const m = dir > 0 ? SOL.moves[S.turn] : {f: SOL.moves[S.turn - 1].f, t: 4 - SOL.moves[S.turn - 1].t};
  play(m, () => { applyMove(S.cube, m); S.turn += dir; if (S.turn >= N) solStop(); if (P.screen === 'solution') { P.state = solState(); render(); } });
}
screens.solution = {
  enter(st, o) {
    solStop();
    if (st === 'end') solveReset(N);
    else if (st === 'stepping' || st === 'playing') solveReset(o.picked ? 9 : S.turn);
    else { solveReset(0); return 'overview'; }
    if (st === 'playing') actions.solPlay();
  },
  render(st) {
    const cap = S.turn === 0 ? t('solution.atStart') : S.turn >= N ? t('solution.atEnd') : t('solve.progress', S.turn + 1, N);
    const list = SOL.stages.map((s, k) => {
      const head = `<div class="shead"><span class="n mono">${k + 1}</span><div><strong>${t('stage.' + (k + 1) + '.title')}</strong> <span class="t-sec">· ${t('solution.stageTurns', s.to - s.from)}</span>
        <p class="note">${t('stage.' + (k + 1) + '.what')} ${t('stage.' + (k + 1) + '.why')}</p>${P.opts.notation ? `<p class="mono t-small" dir="ltr">${SOL.moves.slice(s.from, s.to).map(notation).join(' ')}</p>` : ''}</div></div>`;
      const rows = SOL.moves.slice(s.from, s.to).map((m, j) => {
        const i = s.from + j, d = dirOf(m);
        return `<button type="button" class="turnrow${i === S.turn ? ' cur' : ''}" data-act="solJump" data-arg="${i}" aria-label="${t('solution.a11y.turn', i + 1, t('turn.' + m.f + m.t))}"><span class="n mono">${i + 1}</span>${stk(FACE_COL[m.f], 'swatch')}<span class="grow">${t('side.' + SIDE[m.f])} · ${t('dir.' + d)} · ${t(m.t === 2 ? 'amount.half' : 'amount.quarter')}</span><span dir="ltr">${ico(DIR_ICON[d])}</span></button>`;
      }).join('');
      return head + rows;
    }).join('');
    return `<div class="screen">${topbar(t('solution.title'))}<div class="sol"><div class="sol-left">
      <div class="pad" style="padding:12px 16px 0;gap:2px"><strong>${t('solution.summary', N)}</strong><span class="note">${t('solution.method')}</span></div>
      <div class="sol-cube"><canvas data-scene="solve" role="img" aria-label="${cap}"></canvas></div><div class="sol-cap mono t-sec">${cap}</div>
      <div class="sol-ctl">${btn(t('solution.previous'), 'solPrev', {cls: 'thumb', dis: S.turn === 0})}${SV.playing ? btn(t('solution.pause'), 'solPause', {v: 'primary', cls: 'thumb', icon: 'pause'}) : S.turn >= N ? btn(t('solution.startAgain'), 'solJump', {v: 'primary', cls: 'thumb', arg: '0'}) : btn(t('solution.play'), 'solPlay', {v: 'primary', cls: 'thumb'})}${btn(t('solution.next'), 'solNext', {cls: 'thumb', dis: S.turn >= N})}</div></div>
      <div class="sol-list" data-scroll="sol">${list}</div></div></div>`;
  }
};
Object.assign(actions, {
  solNext() { solStop(); solStep(1); P.state = solState(); render(); },
  solPrev() { solStop(); solStep(-1); P.state = solState(); render(); },
  solJump(el) { solStop(); solveReset(+el.dataset.arg); P.state = solState(); render(); },
  solPlay() { if (S.turn >= N) solveReset(0); SV.playing = true; clearInterval(SV.timer); SV.timer = setInterval(() => { if (P.screen !== 'solution') return solStop(); solStep(1); }, reduced() ? 900 : 700); P.state = 'playing'; render(); },
  solPause() { solStop(); P.state = solState(); render(); }
});

// ---- history ----------------------------------------------------------------------------------
const HIST_SAMPLE = () => [
  {when: Date.now() - 2 * 3600e3, unfinished: 23, side: 'R'},
  {when: Date.now() - 26 * 3600e3, turns: 62, sec: 760, side: 'F'},
  {when: Date.now() - 4 * 86400e3, turns: 104, sec: 1325, side: 'B'},
  {when: Date.now() - 9 * 86400e3, turns: 88, sec: 2011, side: 'L'}
];
P.d.history = HIST_SAMPLE();
P.opts.keepHistory = true;
const histState = () => !P.opts.keepHistory ? 'off' : P.d.history.length ? 'list' : 'empty';
const clock = sec => String(Math.floor(sec / 60)).padStart(2, '0') + ':' + String(sec % 60).padStart(2, '0');
screens.history = {
  enter(st, o) {
    if (st === 'default') return histState();
    if (!o.picked) return;
    P.opts.keepHistory = st !== 'off';
    P.d.history = st === 'empty' ? [] : HIST_SAMPLE();
  },
  render(st) {
    const fmt = new Intl.DateTimeFormat(undefined, {dateStyle: 'medium', timeStyle: 'short'});
    const clear = P.d.history.length && P.opts.keepHistory ? btn(t('history.clear'), 'pick', {v: 'ghost', arg: 'history.confirm-clear'}) : '';
    let body;
    if (!P.opts.keepHistory) body = `<div class="empty"><h2 class="t-title">${t('history.off.title')}</h2><p class="t-sec">${t('history.off.body')}</p>${btn(t('history.off.open'), 'go', {cls: 'thumb', arg: 'settings'})}</div>`;
    else if (!P.d.history.length) body = `<div class="empty"><h2 class="t-title">${t('history.empty.title')}</h2><p class="t-sec">${t('history.empty.body')}</p>${btn(t('home.scan'), 'scan', {v: 'primary', cls: 'thumb', trail: 'chev-r'})}</div>`;
    else body = `<div class="rows">${P.d.history.map((h, i) => `<div class="hrow">${row(esc(fmt.format(h.when)), 'histOpen', String(i), {lead: `<span class="mini" aria-hidden="true">${sideColours(SOL.start, h.side).map(c => stk(c)).join('')}</span>`, sub: h.unfinished ? t('history.item.unfinished', h.unfinished + 1, N) : t('history.item.done', h.turns, clock(h.sec))})}<button type="button" class="iconbtn" data-act="histAsk" data-arg="${i}" aria-label="${t('history.delete')}">${ico('x')}</button></div>`).join('')}</div>`;
    let layer = '';
    if (st === 'confirm-delete') layer = dialog(t('history.delete.title'), '', btn(t('history.delete.keep'), 'closeSheet'), btn(t('history.delete'), 'histDelete', {v: 'ghost'}));
    if (st === 'confirm-clear') layer = dialog(t('history.clear.title'), t('history.clear.body', P.d.history.length), btn(t('history.clear.keep'), 'closeSheet'), btn(t('history.clear.confirm'), 'histClear', {v: 'ghost'}));
    return `<div class="screen">${topbar(t('history.title'), clear)}<div class="page" data-scroll="hist"><div class="col" style="flex:1">${body}</div></div>${layer}</div>`;
  }
};
Object.assign(actions, {
  histOpen(el) { const h = P.d.history[+el.dataset.arg]; if (h.unfinished) { solveReset(h.unfinished); return go('solve', 'hold-resume'); } go('solution', 'overview'); },
  histAsk(el) { P.d.histPick = +el.dataset.arg; P.state = 'confirm-delete'; render(); },
  histDelete() { P.d.history.splice(P.d.histPick || 0, 1); P.state = histState(); render(); },
  histClear() { P.d.history = []; P.state = P.screen === 'settings' ? 'main' : histState(); render(); }
});

// ---- settings -----------------------------------------------------------------------------------
Object.assign(P.opts, {sound: true, haptics: true, pace: 'steady', textPref: 'system'});
screens.settings = {
  enter(st) { if (st === 'default') return 'main'; },
  render(st) {
    const o = P.opts, same = t('settings.theme.system');
    let cam;
    const camState = st.startsWith('camera-') ? st.slice(7) : (touch() ? (P.d.camAllowed ? 'allowed' : 'refused') : 'list');
    if (camState === 'finding') cam = `<div class="status-line" role="status"><span class="dots" aria-hidden="true"><i></i><i></i><i></i></span>${t('settings.camera.finding')}</div>`;
    else if (camState === 'none') cam = notice('caution', t('settings.camera.none'), '', btn(t('common.lookAgain'), 'pick', {icon: 'refresh', arg: 'settings.camera-finding'}));
    else if (camState === 'allowed') cam = notice('ok', t('settings.camera.allowed'), '', btn(t('common.openDeviceSettings'), 'noop'));
    else if (camState === 'refused') cam = notice('caution', t('settings.camera.refused'), '', btn(t('permission.allow'), 'go', {arg: 'permission'}));
    else if (camState === 'unsupported') cam = notice('caution', t('settings.camera.unsupported'), '');
    else cam = `<p class="note">${t('settings.camera.choose')}</p>` + choice(t('settings.section.camera'), [['a', 'Integrated Camera'], ['b', 'USB Camera']], P.d.cam || 'a', 'cam') + `<div>${btn(t('common.lookAgain'), 'pick', {v: 'ghost', icon: 'refresh', arg: 'settings.camera-finding'})}</div>`;
    const dev = st === 'developer' || st === 'restart-needed' ? `<div class="sect"><div class="eyebrow">${t('settings.section.developer')}</div>
      ${notice('caution', t('settings.dev.recording'), '')}
      ${choice(t('settings.dev.backend', 'Direct3D 11'), [['auto', 'Auto'], ['d3d11', 'D3D11'], ['d3d12', 'D3D12'], ['vulkan', 'Vulkan']], st === 'restart-needed' ? 'vulkan' : 'auto', 'backend')}
      ${st === 'restart-needed' ? notice('caution', t('settings.dev.restart'), '') : ''}
      <div class="rows">${sw(t('settings.dev.stats'), '', o.stats, 'stats')}</div></div>` : '';
    const langs = st === 'language' ? sheet(t('settings.section.language'), `<div class="rows">${[['system', t('settings.language.system', LANGS.en)]].concat(Object.entries(LANGS)).map(([k, v]) => `<button type="button" class="row" role="radio" aria-checked="${(P.d.lang || 'system') === k}" data-act="setLang" data-arg="${k}"><span class="grow"${k === 'he' || k === 'ar' ? ' dir="rtl"' : ''}>${v}</span>${(P.d.lang || 'system') === k ? ico('check') : ''}</button>`).join('')}</div>` + btn(t('common.close'), 'closeSheet', {v: 'ghost'})) : '';
    const clear = st === 'confirm-clear' ? dialog(t('history.clear.title'), t('history.clear.body', P.d.history.length), btn(t('history.clear.keep'), 'closeSheet'), btn(t('history.clear.confirm'), 'histClear', {v: 'ghost'})) : '';
    return `<div class="screen">${topbar(t('common.settings'))}<div class="split"><div class="pane page" data-scroll="set"><div class="col">
      <div class="sect"><div class="eyebrow">${t('settings.section.language')}</div><div class="rows">${row(t('settings.section.language'), 'pick', 'settings.language', {val: P.d.lang && P.d.lang !== 'system' ? LANGS[P.d.lang] : t('settings.language.system', LANGS.en)})}</div></div>
      <div class="sect"><div class="eyebrow">${t('settings.section.appearance')}</div>
        ${choice(t('settings.theme'), [['auto', same], ['light', t('settings.theme.light')], ['dark', t('settings.theme.dark')]], o.theme, 'theme')}
        <div class="rows">${sw(t('settings.marks'), t('settings.marks.note'), o.marks, 'marks')}</div>
        <div class="legend" dir="ltr">${Object.keys(MARKS).map(c => `<span role="img" aria-label="${t('colour.' + c)}, ${t('mark.' + MARKS[c])}">${stk(c)}<small>${t('colour.' + c)}</small></span>`).join('')}</div>
        ${choice(t('settings.textSize'), [['system', t('settings.textSize.system')], ['large', t('settings.textSize.large')], ['larger', t('settings.textSize.larger')]], o.textPref, 'textPref')}
        ${choice(t('settings.motion'), [['auto', t('settings.motion.system')], ['reduced', t('settings.motion.reduced')], ['full', t('settings.motion.full')]], o.motion, 'motion')}</div>
      <div class="sect"><div class="eyebrow">${t('settings.section.solving')}</div>
        ${choice(t('settings.advance'), [['tap', t('settings.advance.tap')], ['timer', t('settings.advance.timer')]], o.advance, 'advance')}
        ${o.advance === 'timer' ? choice(t('settings.pace'), [['slow', t('settings.pace.slow')], ['steady', t('settings.pace.steady')], ['quick', t('settings.pace.quick')]], o.pace, 'pace') : ''}
        <div class="rows">${sw(t('settings.explain'), t('settings.explain.note'), o.explain, 'explain')}${sw(t('settings.notation'), t('settings.notation.note'), o.notation, 'notation')}${sw(t('settings.sound'), '', o.sound, 'sound')}${touch() ? sw(t('settings.haptics'), '', o.haptics, 'haptics') : ''}</div></div>
      <div class="sect"><div class="eyebrow">${t('settings.section.camera')}</div>${cam}</div>
      <div class="sect"><div class="eyebrow">${t('settings.section.history')}</div><div class="rows">${sw(t('settings.history.keep'), t('settings.history.note'), o.keepHistory, 'keepHistory')}</div>
        <div>${btn(t('history.clear'), 'pick', {arg: 'settings.confirm-clear', dis: !P.d.history.length})}</div></div>
      <div class="sect"><div class="eyebrow">${t('settings.section.about')}</div><div class="rows">${row(t('settings.about'), 'go', 'about')}${row(t('common.help'), 'go', 'help')}</div></div>${dev}
      </div></div>${cubeAside()}</div>${langs}${clear}</div>`;
  }
};
Object.assign(actions, {
  tog(el) { const k = el.dataset.arg; P.opts[k] = !P.opts[k]; applyOpts(); },
  opt(el) {
    const [k, v] = el.dataset.arg.split('=');
    if (k === 'cam') P.d.cam = v; else if (k === 'backend') P.state = v === 'auto' ? 'developer' : 'restart-needed'; else P.opts[k] = v;
    if (k === 'textPref') P.opts.ts = {system: 1, large: 1.25, larger: 1.5}[v];
    applyOpts();
  },
  setLang(el) {
    P.d.lang = el.dataset.arg; P.opts.dir = P.d.lang === 'he' || P.d.lang === 'ar' ? 'rtl' : 'ltr';
    const prev = P.stack[P.stack.length - 1];
    if (prev && prev.screen === 'first-run') { P.stack.pop(); P.screen = 'first-run'; P.state = prev.state; } else P.state = 'main';
    applyOpts();
  }
});

// ---- about, licences, privacy ------------------------------------------------------------------
// A stand-in list. The real one is generated by the build from what actually ships (E0b).
const LIC = [['Qt', 'LGPL-3.0-only or GPL-3.0-only'], ['Qt Quick 3D', 'GPL-3.0-only'], ['OpenCV', 'Apache-2.0'], ['Archivo', 'OFL-1.1'], ['IBM Plex', 'OFL-1.1'], ['Lucide', 'ISC']];
screens.about = {
  render() {
    return `<div class="screen">${topbar(t('about.title'))}<div class="page" data-scroll="about"><div class="col">
      <div class="lockup"><span class="appicon" aria-hidden="true">${IDENTITY.icon || ''}</span><div><h1 class="t-title">${t('app.name')}</h1><div class="mono t-sec">${t('about.version', '1.0.0')}</div></div></div>
      <div class="prose"><p>${t('about.what')}</p><p>${t('about.author', 'Iain Chesworth')}</p><p class="t-sec">${t('about.licence')}</p></div>
      <div class="rows">${row(t('about.licences'), 'go', 'licences')}${row(t('about.privacy'), 'go', 'privacy')}${row(t('about.source'), 'noop', '', {val: 'github.com/iainchesworth/cubeulator'})}</div>
      <p class="note">${t('about.trademark')}</p></div></div></div>`;
  }
};
screens.licences = {
  enter(st) { if (st === 'default') return 'list'; },
  render(st) {
    let body;
    if (st === 'unavailable') body = notice('caution', t('licences.unavailable.title'), t('licences.unavailable.body', 'github.com/iainchesworth/cubeulator'));
    else if (st === 'detail') { const l = LIC[P.d.lic || 0]; body = `<h2 class="t-headline">${l[0]}</h2><p class="mono t-sec" dir="ltr">${l[1]}</p><pre class="lic" dir="ltr">The component's own copyright notice and licence text appear here in full, exactly as its authors wrote them. They are never translated.</pre>`; }
    else body = `<p class="t-sec">${t('licences.intro')}</p><p class="tagline">stand-in list: the real one is generated by the build</p><div class="rows">${row(t('licences.own'), 'licOpen', '-1', {val: 'GPL-3.0-or-later'})}${LIC.map((l, i) => row(l[0], 'licOpen', String(i), {val: l[1]})).join('')}</div>`;
    return `<div class="screen">${topbar(t('licences.title'))}<div class="page" data-scroll="lic"><div class="col">${body}</div></div></div>`;
  }
};
actions.licOpen = el => { P.d.lic = Math.max(0, +el.dataset.arg); P.stack.push({screen: 'licences', state: 'list'}); P.state = 'detail'; render(); };
screens.privacy = {
  render() {
    const part = k => `<div><h2 class="t-headline">${t('privacy.' + k + '.title')}</h2><p style="margin-top:6px">${t('privacy.' + k + '.body')}</p></div>`;
    return `<div class="screen">${topbar(t('privacy.title'))}<div class="page" data-scroll="priv"><div class="col prose">${part('camera')}${part('history')}${part('network')}</div></div></div>`;
  }
};

// ---- help -------------------------------------------------------------------------------------------
const TOPICS = ['scanning', 'entering', 'holding', 'turns', 'lost', 'cubes', 'notation', 'keys'];
screens.help = {
  enter(st) { if (st === 'default') return 'topics'; },
  render(st) {
    if (st === 'topics') {
      return `<div class="screen">${topbar(t('help.title'))}<div class="page" data-scroll="help"><div class="col"><div class="rows">${row(t('help.howItWorks'), 'pick', 'first-run.revisit')}${TOPICS.filter(k => k !== 'keys' || !touch()).map(k => row(t('help.' + k), 'go', 'help.' + k)).join('')}</div></div></div></div>`;
    }
    const pic = st === 'holding' ? `<div style="height:200px;position:relative"><canvas data-scene="held" role="img" aria-label="${t('solve.hold.body')}" style="position:absolute;inset:0"></canvas></div>${holdLine()}` : '';
    return `<div class="screen">${topbar(t('help.' + st))}<div class="page" data-scroll="helpt"><div class="col">${pic}<div class="prose">${prose(t('help.' + st + '.body'))}</div></div></div></div>`;
  }
};
scenes.held = {cube: newCube(), live: () => false, draw(ctx, w, h) { drawCube(ctx, this.cube, {yaw: YAW, pitch: PITCH, cx: w / 2, cy: h / 2, s: Math.min(w, h) / 5.6}); }};

Object.assign(actions, {
  firstRunDone() { try { localStorage.setItem('cubeulator-proto-seen', '1'); } catch (e) {} P.stack = []; go('home', 'default', {push: false}); },
  permAllow() { P.d.camAllowed = true; P.d.permRefused = false; const prev = P.stack[P.stack.length - 1]; if (prev && prev.screen === 'settings') return popBack(); go('scan', 'starting', {push: false}); },
  permDeny() { P.d.permRefused = true; P.state = 'refused'; render(); }
});
