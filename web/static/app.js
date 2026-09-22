'use strict';
/* BroWatch Web v0.2 — SquachWare UI, полный паритет с прошивкой.
   Маскот: LILGUY 1:1 из прошивки (include/lil_guy.h): 8 кадров 10x10,
   2 бита на пиксель, кадр = (now/80)%8. Цвета из Theme::drawLilGuy:
   hair #00FFF5, skin #FFD0F0, body #B967FF.
   Фоны: подмножество Settings::Background. Цвета типов — Theme::colorFor,
   имена — TypeNames::ru.
   Шаблоны: CANNED_RU[] 0..49 = src/meshwords.cpp (48/49 — реакции
   LIKE/DISLIKE, шлются в эфир как canned). Табы шаблонов = CANNED_TAB.
   Эмоции: 35 штук MeshMsg::Emote (meshmsg.h), имена/табы/RU-ярлыки =
   EmoteScript NAME/NAME_RU/TAB (src/emote_script.cpp). Мост привозит имя
   (NAME[i][0]); индекс для отправки восстанавливаем по EMOTE_BY_NAME.
   Presence: пир «в эфире», пока свежак (плата держит 12 с, сервер — 15 с);
   heartbeat веба каждые 10 с. */
const $ = id => document.getElementById(id);
const chatEl = $('chat'), peersBox = $('squad'), recentBox = $('recent'),
      detsBox = $('dets'), statusEl = $('status'), boardPill = $('board-name');

/* ---------- prefs ---------- */
const store = {
  get(k, d){ try{ const v = localStorage.getItem('bw_'+k); return v === null ? d : v; }catch(e){ return d; } },
  set(k, v){ try{ localStorage.setItem('bw_'+k, v); }catch(e){} }
};
const myname = () => ($('myname').value || 'web').slice(0, 24);
$('myname').value = store.get('name', 'web');

/* ---------- helpers ---------- */
function esc(s){ return String(s ?? '').replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c])); }
const tHHMM = ts => new Date(ts * 1000).toLocaleTimeString('ru-RU', {hour:'2-digit', minute:'2-digit'});
const dayKey = ts => { const d = new Date(ts * 1000); return d.getFullYear()+'-'+d.getMonth()+'-'+d.getDate(); };
const dayName = ts => new Date(ts * 1000).toLocaleDateString('ru-RU', {day:'numeric', month:'long', weekday:'short'});
const ago = ts => {
  const s = Math.max(0, Math.round(Date.now()/1000 - ts));
  if(s < 5) return 'только что';
  if(s < 60) return s + ' с назад';
  const m = Math.round(s/60);
  if(m < 60) return m + ' мин назад';
  return Math.round(m/60) + ' ч назад';
};

/* ---------- RU имена и цвета типов (TypeNames::ru, Theme::colorFor) ---------- */
const TYPE_RU = {FLOCK:'ФЛОК',AXON:'АКСОН',META:'МЕТА',SKIMMER:'СКИММЕР',RAVEN:'РЕЙВЕН',
  AIRTAG:'ЭЙРТАГ',DRONE:'ДРОН',ALPR:'АЛПР',CAMERA:'КАМЕРА',SAMSUNG_TAG:'СМАРТТАГ',
  GOOGLE_TAG:'ГУГЛ-ТАГ',TILE:'ТАЙЛ',RING:'РИНГ',DEAUTH:'ДЕАУТ',EVILTWIN:'ДВОЙНИК',
  IBEACON:'МАЯК',HACKER:'ХАКЕР'};
const TYPE_COLOR = {};
['FLOCK','AXON','META'].forEach(t => TYPE_COLOR[t] = '#ff2d78');
TYPE_COLOR.SKIMMER = '#fffb96';
['RAVEN','ALPR'].forEach(t => TYPE_COLOR[t] = '#ffa600');
['AIRTAG','DRONE','SAMSUNG_TAG','GOOGLE_TAG','TILE','IBEACON'].forEach(t => TYPE_COLOR[t] = '#b967ff');
['CAMERA','RING'].forEach(t => TYPE_COLOR[t] = '#00fff5');
['DEAUTH','EVILTWIN','HACKER'].forEach(t => TYPE_COLOR[t] = '#ff0000');
const typeColor = t => TYPE_COLOR[t] || '#00ff88';

/* ================= ЭМОЦИИ: 35 MeshMsg::Emote, порядок = прошивке ================= */
const EMOTES = [
  ['WAVE','👋','Машет'],['HIGH FIVE','🙏','Дай пять'],['DANCE-OFF','💃','Танцы'],['ROCK PAPER','✊','Цу-е-фа'],
  ['SNOWBALL','❄️','Снежок'],['BOO!','👻','Бу!'],['FIST BUMP','👊','Кулак'],['SECRET','🤝','Секретный знак'],
  ['SALUTE','🫡','Салют'],['BOW','🙇','Поклон'],['HUG','🫂','Обнимашки'],['COIN FLIP','🪙','Монетка'],
  ['DICE ROLL','🎲','Кости'],['ARM','💪','Армрестлинг'],['TUG OF WAR','🪢','Канат'],['LEAPFROG','🐸','Чехарда'],
  ['PIE','🥧','Торт в лицо'],['WATER','🎈','Шарик с водой'],['PAPER','✈️','Самолётик'],['PILLOW','🛌','Бой подушками'],
  ['GIFT','🎁','Подарок'],['SNACK','🍪','Вкусняшка'],['CHEERS','🥂','Ура'],['CONFETTI','🎉','Конфетти'],
  ['FIREWORKS','🎆','Фейерверк'],['HEART','❤️','Сердце'],['LAUGH','😂','Хохот'],['SAD','😢','Грусть'],
  ['GRR','😠','Рык'],['SLEEPY','😴','Спать'],['TINFOIL','👽','Шапочка из фольги'],['CAMERA!','📸','Фото!'],
  ['SEE THAT?','👀','Видишь?'],['HOWL','🐺','Вой'],['SELFIE','🤳','Селфи']];
const EMOTE_BY_NAME = {};
EMOTES.forEach((e, i) => { EMOTE_BY_NAME[e[0]] = i; });
/* Табы пикета платы (TAB_NAME_RU + TAB): ПРИВЕТ/ИГРА/ПРИКОЛ/ТУСА/НАСТРОЙ/ГЛЯДИ. */
const EMOTE_TABS = [['ПРИВЕТ',[0,1,6,7,8,9]],['ИГРА',[3,11,12,13,14,15]],
  ['ПРИКОЛ',[4,16,17,18,19]],['ТУСА',[10,20,21,22,23,24]],
  ['НАСТРОЙ',[2,25,26,27,28,29]],['ГЛЯДИ',[5,30,31,32,33,34]]];
const SPIN_E = new Set([2,21,22,23,24,26,33]);
const SHAKE_E = new Set([5,27,28,30,32]);
const emoteLabel = nm => {
  const i = EMOTE_BY_NAME[String(nm || '').toUpperCase()];
  return i === undefined ? null : {i, e:EMOTES[i][1], ru:EMOTES[i][2]};
};

/* ================= LILGUY: точные данные прошивки ================= */
const LILGUY = [
  0x000000,0x001540,0x000950,0x002A40,0x000E40,0x000E40,0x000E00,0x000F00,0x000F80,0x000A00,
  0x001100,0x000540,0x000950,0x002A40,0x000E40,0x000F90,0x008F80,0x008F00,0x0023C0,0x000280,
  0x000000,0x000440,0x000550,0x000940,0x002A40,0x000E90,0x000F80,0x003FA0,0x003FC0,0x00A0A0,
  0x000000,0x000500,0x001950,0x002A50,0x000E40,0x000E40,0x000F80,0x000F00,0x003FE0,0x002820,
  0x000000,0x001540,0x000950,0x002A50,0x000E40,0x000B40,0x000E00,0x000F00,0x000F80,0x000A00,
  0x001100,0x000540,0x000950,0x002A40,0x000E40,0x000B40,0x008B00,0x008F00,0x0023C0,0x000280,
  0x000000,0x000440,0x000550,0x000940,0x002A40,0x000B40,0x000B00,0x002F00,0x003FC0,0x00A0A0,
  0x000000,0x000500,0x001950,0x002A50,0x000E40,0x000E40,0x000B00,0x000F00,0x003FE0,0x002820];
const LIL_PAL = [null, '#00fff5', '#ffd0f0', '#b967ff'];
function drawLilGuy(ctx, f, x, y, s, flip){
  for(let yy = 0; yy < 10; yy++){
    const row = LILGUY[f * 10 + yy] >>> 0;
    for(let xx = 0; xx < 10; xx++){
      const c = (row >>> (xx * 2)) & 3;
      if(!c) continue;
      const dx = flip ? 9 - xx : xx;
      ctx.fillStyle = LIL_PAL[c];
      ctx.fillRect(x + dx * s, y + yy * s, s, s);
    }
  }
}
/* Маскот в шапке: цикл 80 мс, как на плате. hop/spin/shake — реакции. */
const mascot = $('mascot').getContext('2d');
(function mascotLoop(){
  const f = Math.floor(performance.now() / 80) % 8;
  mascot.clearRect(0, 0, 70, 70);
  drawLilGuy(mascot, f, 0, 0, 7, false);
  requestAnimationFrame(mascotLoop);
})();
function react(cls){
  const m = $('mascot');
  m.classList.remove('hop', 'spin', 'shake'); void m.offsetWidth;
  m.classList.add(cls || 'hop');
}
function emoteReact(nm){
  const l = emoteLabel(nm);
  react(!l ? 'hop' : SPIN_E.has(l.i) ? 'spin' : SHAKE_E.has(l.i) ? 'shake' : 'hop');
}

/* ================= фоны ================= */
const BG_MODES = [
  ['black','Выкл'],['digital','Дождь'],['starfield','Звёзды'],['toasters','Тостеры'],
  ['fireflies','Светлячки'],['snowfall','Снег'],['spectrum','Спектр'],['synthwave','Синтвейв']];
const bgCanvas = $('bg'), bctx = bgCanvas.getContext('2d');
if(!CanvasRenderingContext2D.prototype.roundRect){
  CanvasRenderingContext2D.prototype.roundRect = function(x,y,w,h){ this.rect(x,y,w,h); return this; };
}
let bgMode = store.get('bg', 'digital');
let motionOn = store.get('motion', matchMedia('(prefers-reduced-motion: reduce)').matches ? '0' : '1') === '1';
let bgParts = [], specData = [];
function bgSize(){
  const d = Math.min(devicePixelRatio || 1, 2);
  bgCanvas.width = innerWidth * d; bgCanvas.height = innerHeight * d;
  bctx.setTransform(d, 0, 0, d, 0, 0);
  bgInit();
}
function rnd(a, b){ return a + Math.random() * (b - a); }
function bgInit(){
  bgParts = [];
  const W = innerWidth, H = innerHeight;
  if(bgMode === 'digital'){
    const cols = Math.floor(W / 16);
    for(let i = 0; i < cols; i++) bgParts.push({x:i*16, y:rnd(-H,0), v:rnd(2,6)});
  }else if(bgMode === 'starfield'){
    for(let i = 0; i < Math.min(160, W/6); i++) bgParts.push({x:rnd(0,W), y:rnd(0,H), v:rnd(.1,.6), r:rnd(.5,1.6)});
  }else if(bgMode === 'snowfall'){
    for(let i = 0; i < Math.min(120, W/8); i++) bgParts.push({x:rnd(0,W), y:rnd(0,H), v:rnd(.4,1.4), r:rnd(1,3), ph:rnd(0,6)});
  }else if(bgMode === 'fireflies'){
    for(let i = 0; i < Math.min(46, W/22); i++) bgParts.push({x:rnd(0,W), y:rnd(0,H), vx:rnd(-.3,.3), vy:rnd(-.25,.25), ph:rnd(0,6)});
  }else if(bgMode === 'toasters'){
    for(let i = 0; i < 7; i++) bgParts.push({x:rnd(0,W), y:rnd(0,H*.7), v:rnd(.3,1), s:rnd(10,22)});
  }
}
const GLYPHS = '01アイチエミカクシスセソタ01<>*+#';
let bgT = 0;
function bgFrame(){
  requestAnimationFrame(bgFrame);
  if(!motionOn || document.hidden) return;
  const W = innerWidth, H = innerHeight;
  bctx.clearRect(0, 0, W, H);
  bgT++;
  if(bgMode === 'black') return;
  if(bgMode === 'digital'){
    bctx.font = '14px monospace';
    for(const p of bgParts){
      bctx.fillStyle = '#00ff8855';
      bctx.fillText(GLYPHS[(Math.random()*GLYPHS.length)|0], p.x, p.y);
      bctx.fillStyle = '#00fff5';
      bctx.fillRect(p.x+2, p.y-12, 8, 2);
      p.y += p.v; if(p.y > H+20){ p.y = rnd(-80,-10); p.v = rnd(2,6); }
    }
  }else if(bgMode === 'starfield'){
    for(const p of bgParts){
      bctx.fillStyle = '#ffffffaa'; bctx.fillRect(p.x, p.y, p.r, p.r);
      p.x -= p.v; if(p.x < 0){ p.x = W; p.y = rnd(0,H); }
    }
  }else if(bgMode === 'snowfall'){
    for(const p of bgParts){
      bctx.fillStyle = '#a0dcffcc';
      bctx.beginPath(); bctx.arc(p.x + Math.sin(bgT/40+p.ph)*12, p.y, p.r, 0, 7); bctx.fill();
      p.y += p.v; if(p.y > H+5){ p.y = -5; p.x = rnd(0,W); }
    }
  }else if(bgMode === 'fireflies'){
    for(const p of bgParts){
      const a = .35 + .3*Math.sin(bgT/30+p.ph);
      bctx.fillStyle = `rgba(200,255,0,${a})`;
      bctx.beginPath(); bctx.arc(p.x, p.y, 2.4, 0, 7); bctx.fill();
      p.x += p.vx; p.y += p.vy;
      if(p.x<0)p.x=W; if(p.x>W)p.x=0; if(p.y<0)p.y=H; if(p.y>H)p.y=0;
    }
  }else if(bgMode === 'synthwave'){
    const g = bctx.createLinearGradient(0,0,0,H);
    g.addColorStop(0,'#0a000f'); g.addColorStop(.55,'#2a0a3a'); g.addColorStop(.62,'#ff2d78'); g.addColorStop(.66,'#0a000f');
    bctx.fillStyle = g; bctx.fillRect(0,0,W,H);
    const sx=W/2, sr=Math.min(W,H)*.16, sy=H*.5;
    const sg = bctx.createLinearGradient(0,sy-sr,0,sy+sr);
    sg.addColorStop(0,'#fffb96'); sg.addColorStop(1,'#ff2d78');
    bctx.fillStyle = sg; bctx.beginPath(); bctx.arc(sx,sy,sr,0,7); bctx.fill();
    bctx.fillStyle = '#0a000f';
    for(let i=0;i<4;i++) bctx.fillRect(sx-sr, sy+sr*.15+i*sr*.18+((bgT/8)%(sr*.18)), sr*2, 2);
    bctx.strokeStyle = '#b967ff88'; bctx.lineWidth = 1;
    for(let i=0;i<=12;i++){ const x=(i/12-.5)*W*2+W/2; bctx.beginPath(); bctx.moveTo(W/2,H*.62); bctx.lineTo(x,H); bctx.stroke(); }
    for(let i=0;i<6;i++){ const y=H*.62+Math.pow(i/6,1.6)*H*.4; bctx.beginPath(); bctx.moveTo(0,y); bctx.lineTo(W,y); bctx.stroke(); }
  }else if(bgMode === 'toasters'){
    for(const p of bgParts){
      bctx.fillStyle = '#c8a24bdd';
      bctx.beginPath(); bctx.roundRect(p.x, p.y, p.s*2, p.s, 4); bctx.fill();
      bctx.fillStyle = '#8a6a2a'; bctx.fillRect(p.x+3, p.y+3, 4, 4); bctx.fillRect(p.x+p.s*2-7, p.y+3, 4, 4);
      p.x -= p.v; if(p.x < -p.s*2){ p.x = W+10; p.y = rnd(0,H*.7); }
    }
    const f = Math.floor(performance.now()/80) % 8;
    const lx = W - ((bgT*.8) % (W+80)) + 40;
    drawLilGuy(bctx, f, lx, H-46, 4, true);
  }else if(bgMode === 'spectrum'){
    const n = Math.max(specData.length, 1), bw = W/Math.max(n,24);
    for(let i = 0; i < Math.max(n,24); i++){
      const d = specData[i % n];
      const h = d ? Math.min(H*.7, Math.max(6, (100 + d.rssi) * H/140)) : 4;
      bctx.fillStyle = d ? typeColor(d.type) : '#333355';
      bctx.fillRect(i*bw+1, H-h-60, Math.max(2,bw-2), h);
    }
  }
}
addEventListener('resize', bgSize);
bgSize(); requestAnimationFrame(bgFrame);

/* ---------- sheet: фон, анимация, уведомления ---------- */
function drawBgPick(){
  $('bg-pick').innerHTML = BG_MODES.map(([k,n]) =>
    `<button data-bg="${k}" class="${k===bgMode?'on':''}">${n}</button>`).join('');
  $('bg-pick').querySelectorAll('button').forEach(b => b.onclick = () => {
    bgMode = b.dataset.bg; store.set('bg', bgMode); bgInit(); drawBgPick();
  });
}
$('motion').checked = motionOn;
$('motion').onchange = e => { motionOn = e.target.checked; store.set('motion', motionOn?'1':'0'); };
const notifPref = () => store.get('notif', 'ask');
const setNotif = v => { store.set('notif', v); $('notif').checked = v === 'on'; };
$('notif').checked = notifPref() === 'on';
$('notif').onchange = async e => {
  if(!('Notification' in window)){ e.target.checked = false; toast('Браузер не умеет в уведомления'); return; }
  if(e.target.checked){
    try{
      const p = await Notification.requestPermission();
      setNotif(p === 'granted' ? 'on' : 'off');
      if(p !== 'granted') toast('Уведомления запрещены браузером');
    }catch(_){ setNotif('off'); }
  } else setNotif('off');
};
$('btn-bg').onclick = () => { drawBgPick(); $('sheet').hidden = false; };
$('sheet-close').onclick = () => $('sheet').hidden = true;
$('sheet').addEventListener('click', e => { if(e.target.id === 'sheet') $('sheet').hidden = true; });

/* ---------- тосты + системные уведомления ---------- */
function toast(text, tab){
  const box = $('toasts');
  while(box.children.length >= 3) box.firstChild.remove();
  const d = document.createElement('div');
  d.className = 'toast'; d.textContent = text;
  if(tab) d.onclick = () => gotoTab(tab);
  box.appendChild(d);
  setTimeout(() => d.remove(), 4500);
}
async function notifySys(title, body, tag){
  if(notifPref() === 'off' || !document.hidden) return;
  if(!('Notification' in window)) return;
  if(notifPref() === 'ask'){
    try{
      const p = await Notification.requestPermission();
      setNotif(p === 'granted' ? 'on' : 'off');
    }catch(_){ setNotif('off'); return; }
  }
  if(notifPref() !== 'on') return;
  try{
    const n = new Notification(title, {body, tag, icon:'/static/icon-192.png', badge:'/static/icon-192.png'});
    n.onclick = () => { try{ window.focus(); }catch(_){} n.close(); };
  }catch(_){}
}

/* ================= табы ================= */
const DESKTOP = matchMedia('(min-width:1024px)');
let activeTab = 'chat';
function gotoTab(t){
  activeTab = t;
  document.querySelectorAll('#tabs button').forEach(x => x.classList.toggle('on', x.dataset.tab === t));
  document.querySelectorAll('.tab').forEach(s => s.classList.toggle('on', s.id === 'tab-'+t));
  if(t === 'chat') seenChat();
}
document.querySelectorAll('#tabs button').forEach(b => b.onclick = () => gotoTab(b.dataset.tab));

/* ================= данные ================= */
let lastMsgId = 0, lastEmoTs = 0, lastDetTs = 0;
let msgsCache = [], peersCache = {}, boardOnline = false, boardInfo = null;
let unread = 0, chatAtBottom = true, sseLive = false, tickBusy = false, fullSyncDone = false;
async function jget(p){ const r = await fetch(p); if(!r.ok) throw 0; return r.json(); }
async function jpost(p, o){
  const r = await fetch(p, {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(o)});
  if(!r.ok) throw await r.json().catch(() => 0); return r.json();
}
const peerName = id => (peersCache[id] && (peersCache[id].name || id)) || id;

/* ---------- чат-пузыри ---------- */
const REACT_TXT = {'Понравилось.':['👍','Понравилось.'], 'Не понравилось.':['👎','Не понравилось.']};
function msgBubble(m){
  if(m.kind === 'emote'){
    const l = emoteLabel(m.text);
    const face = l ? l.e : '✨', name = l ? l.ru.toLowerCase() : String(m.text).toLowerCase();
    return `<div class="bubble emote-b"><span class="eface">${esc(face)}</span><span class="elabel">${esc(name)}</span></div>`;
  }
  const r = REACT_TXT[m.text];
  if(r) return `<div class="bubble react-b"><span class="eface">${r[0]}</span><span class="elabel">${esc(r[1])}</span></div>`;
  return `<div class="bubble">${esc(m.text)}</div>`;
}
function renderChat(){
  const nearBottom = chatEl.scrollHeight - chatEl.scrollTop - chatEl.clientHeight < 80;
  const items = msgsCache.slice(-200);
  let html = '', lastDay = '';
  for(const m of items){
    const dk = dayKey(m.ts);
    if(dk !== lastDay){ lastDay = dk; html += `<div class="day">${esc(dayName(m.ts))}</div>`; }
    const me = m.from === myname() || m.from === 'web:'+myname();
    const via = m.via === 'board' ? '<span class="via">в эфир</span>'
      : m.via === 'mesh' ? '<span class="via">эфир</span>' : '';
    const cls = m.kind === 'emote' ? 'msg emote' : 'msg';
    html += `<div class="${cls}${me?' me':''}">${msgBubble(m)}<div class="meta"><b>${esc(peerName(m.from))}</b>${via}<span>${tHHMM(m.ts)}</span></div></div>`;
  }
  if(!items.length) html = '<div class="empty">Пока тихо. Напишите первым — плата услышит через мост.</div>';
  chatEl.innerHTML = html;
  if(nearBottom || chatAtBottom){ chatEl.scrollTop = chatEl.scrollHeight; $('tonew').hidden = true; }
  else $('tonew').hidden = unread === 0;
}
$('tonew').onclick = () => { seenChat(); chatEl.scrollTop = chatEl.scrollHeight; };
chatEl.addEventListener('scroll', () => {
  chatAtBottom = chatEl.scrollHeight - chatEl.scrollTop - chatEl.clientHeight < 80;
  if(chatAtBottom) seenChat();
  else $('tonew').hidden = unread === 0;
});
function seenChat(){
  unread = 0; chatAtBottom = true;
  $('unread').hidden = true; $('tonew').hidden = true; document.title = 'BroWatch Web';
}
function notify(n){
  unread += n; react('hop');
  const visible = DESKTOP.matches || activeTab === 'chat';
  if(!(visible && chatAtBottom)){
    $('unread').hidden = false; $('unread').textContent = unread;
    document.title = `(${unread}) BroWatch Web`;
    if(!chatAtBottom) $('tonew').hidden = false;
  }
}

/* ---------- входящие ---------- */
function onMessage(m, live){
  if(m.id > lastMsgId) lastMsgId = m.id;
  if(!msgsCache.some(k => k.id === m.id)){
    msgsCache.push(m); msgsCache = msgsCache.slice(-300);
    if(live && m.from !== myname() && m.from !== 'web:'+myname()){
      notify(1); react('hop');
      const t = `${peerName(m.from)}: ${m.text}`;
      toast(t, 'chat'); notifySys('BroWatch: ' + peerName(m.from), m.text, 'msg'+m.id);
    }
  }
  if(live) renderChat();
}
function onEmotion(e, live){
  const l = emoteLabel(e.emote);
  const m = {kind:'emote', from:e.from, text:e.emote, ts:e.ts, id:'e'+e.ts+e.from+e.emote};
  if(!msgsCache.some(k => k.id === m.id)){
    msgsCache.push(m); msgsCache = msgsCache.slice(-300);
    if(e.ts > lastEmoTs) lastEmoTs = e.ts;
    if(live && e.from !== myname() && e.from !== 'web:'+myname()){
      notify(1); emoteReact(e.emote);
      const t = `${peerName(e.from)}: ${l ? l.e+' '+l.ru.toLowerCase() : e.emote}`;
      toast(t, 'chat'); notifySys('BroWatch: ' + peerName(e.from), l ? l.e+' '+l.ru : e.emote, 'emo'+e.ts);
    } else if(live) emoteReact(e.emote);
  }
  if(live) renderChat();
}
function onDetection(d, live){
  if(d.ts > lastDetTs) lastDetTs = d.ts;
  if(live){
    const title = TYPE_RU[d.type] || d.type || '?';
    toast(`📡 ${title} · ${d.rssi ?? ''} ${d.vendor || ''}`.trim(), 'radar');
    notifySys('BroWatch: детекция', `${title} · ${d.vendor || d.mac || ''}`, 'det'+d.ts);
    renderDets(); gotoTabKeep();
  }
}
function gotoTabKeep(){ /* радар-счётчик обновится в renderDets */ }

/* ---------- squad: как на плате — сначала кто здесь ---------- */
function squadRow(x){
  const last = x.last_msg ? `<div class="last">${esc(x.last_msg.text)}</div>` : '';
  const tag = x.in_range ? '<span class="tag squad">в эфире</span>' : `<span class="tag range">${esc(ago(x.last_seen))}</span>`;
  return `<li><div class="top"><span class="dot${x.in_range?' in':''}"></span>`+
    `<span class="nm">${esc(x.name || x.id)}</span>${tag}`+
    `<span class="rssi">${x.rssi ?? ''}</span></div>${last}</li>`;
}
function renderSquad(){
  const list = Object.values(peersCache);
  const here = list.filter(p => p.in_range && !isBoardPeer(p));
  const gone = list.filter(p => !p.in_range && !isBoardPeer(p));
  $('squad-count').textContent = here.length || '';
  peersBox.innerHTML = here.length ? here.map(squadRow).join('')
    : '<div class="empty">Рядом никого — только вы. Члены отряда появятся сами, когда их услышит плата.</div>';
  recentBox.innerHTML = gone.length ? gone.map(squadRow).join('')
    : '<div class="empty">Все, кого слышали, до сих пор в эфире.</div>';
  $('range-count').textContent = here.length ? '· ' + here.length : '';
  const b = boardInfo;
  if(boardOnline && b){
    $('board-pill').hidden = false; boardPill.textContent = b.name || b.id;
    const bc = $('board-card'); bc.hidden = false;
    bc.innerHTML = `<div class="top"><span class="dot in"></span><span class="nm">⚡ ${esc(b.name || b.id)}</span>`+
      `<span class="tag squad">плата</span></div><div class="last">${esc(b.client || '')} · язык ${esc(b.lang || '?')}</div>`;
  } else {
    $('board-pill').hidden = true; $('board-card').hidden = true;
  }
  $('foot-board').textContent = 'плата: ' + (boardOnline && b ? (b.name || b.id) : 'не в эфире');
}
const isBoardPeer = p => (p.client || '').startsWith('bw ');
function onSquad(list, live){
  const wasBoard = boardOnline;
  peersCache = {};
  boardOnline = false; boardInfo = null;
  for(const p of list){
    peersCache[p.id] = p;
    if(isBoardPeer(p) && p.in_range){ boardOnline = true; boardInfo = p; }
  }
  if(live && boardOnline !== wasBoard){
    if(boardOnline){ toast('⚡ Плата в эфире: ' + (boardInfo.name || '')); notifySys('BroWatch', 'Плата подключена: ' + (boardInfo.name || ''), 'board'); }
    else { toast('Плата пропала из эфира'); notifySys('BroWatch', 'Плата отключена', 'board'); }
  }
  renderSquad();
}
function onLeave(id){
  delete peersCache[id];
  renderSquad();
}

/* ---------- радар ---------- */
let detsCache = [];
function renderDets(){
  specData = detsCache.slice(-32);
  const show = detsCache.slice(-30).reverse();
  $('det-count').textContent = detsCache.length || '';
  if(!show.length){ detsBox.innerHTML = '<div class="empty">Детекций пока нет. Плата пришлёт их сюда по USB-мосту.</div>'; return; }
  detsBox.innerHTML = show.map(x => {
    const c = typeColor(x.type || '');
    const lvl = x.rssi == null ? 0 : x.rssi >= -60 ? 4 : x.rssi >= -70 ? 3 : x.rssi >= -80 ? 2 : 1;
    const bars = `<span class="rssi-bars">${[1,2,3,4].map(i =>
      `<i style="height:${3+i*3}px;${i>lvl?'background:#444':''}"></i>`).join('')}</span>`;
    return `<li><span class="badge" style="background:${c}">${esc(TYPE_RU[x.type] || x.type || '?')}</span>`+
      `<span class="mac">${esc(x.mac || '')}</span>`+
      `<div class="sub"><span>${bars} ${x.rssi ?? ''}</span><span>${esc(x.vendor || '')}</span><span>${tHHMM(x.ts)}</span></div></li>`;
  }).join('');
}

/* ---------- sync: SSE primary, poll fallback ---------- */
async function fullSync(){
  const [msgs, emos, squad, dets, health] = await Promise.all([
    jget('/api/messages?since=0').catch(() => []),
    jget('/api/emotions').catch(() => []),
    jget('/api/squad').catch(() => []),
    jget('/api/detections').catch(() => []),
    jget('/api/health').catch(() => null),
  ]);
  msgsCache = msgs.slice(-300);
  for(const m of msgsCache) if(m.id > lastMsgId) lastMsgId = m.id;
  for(const e of emos.slice(-30)){
    msgsCache.push({kind:'emote', from:e.from, text:e.emote, ts:e.ts, id:'e'+e.ts+e.from+e.emote});
    if(e.ts > lastEmoTs) lastEmoTs = e.ts;
  }
  msgsCache.sort((a,b) => a.ts - b.ts);
  detsCache = dets.slice(-100);
  for(const d of detsCache) if(d.ts > lastDetTs) lastDetTs = d.ts;
  onSquad(squad, false);
  if(health && health.version) $('ver').textContent = 'v' + health.version;
  renderChat(); renderSquad(); renderDets();
  fullSyncDone = true;
}
async function tick(){
  if(tickBusy) return; tickBusy = true;
  try{
    if(!fullSyncDone){ await fullSync(); }
    else if(!sseLive){
      const [msgs, emos, squad, dets] = await Promise.all([
        jget('/api/messages?since=' + lastMsgId).catch(() => []),
        jget('/api/emotions?since=' + lastEmoTs).catch(() => []),
        jget('/api/squad').catch(() => []),
        jget('/api/detections?since=' + lastDetTs).catch(() => []),
      ]);
      for(const m of msgs) onMessage(m, true);
      for(const e of emos) onEmotion(e, true);
      if(dets.length){ detsCache.push(...dets); detsCache = detsCache.slice(-100); for(const d of dets) onDetection(d, true); }
      onSquad(squad, true);
    }
    statusEl.textContent = (boardOnline ? '⚡ плата · ' : '') + 'онлайн · ' + new Date().toLocaleTimeString('ru-RU');
    statusEl.className = 'ok';
  }catch(e){
    statusEl.textContent = 'ошибка сети'; statusEl.className = 'err';
  }
  tickBusy = false;
}
function connectSSE(){
  let es;
  try{ es = new EventSource('/api/stream'); }catch(_){ return; }
  es.addEventListener('hello', () => { sseLive = true; });
  es.addEventListener('message', ev => { try{ onMessage(JSON.parse(ev.data), true); }catch(_){} });
  es.addEventListener('emotion', ev => { try{ onEmotion(JSON.parse(ev.data), true); }catch(_){} });
  es.addEventListener('detection', ev => {
    try{
      const d = JSON.parse(ev.data);
      detsCache.push(d); detsCache = detsCache.slice(-100); onDetection(d, true);
    }catch(_){}
  });
  es.addEventListener('peer', ev => {
    try{
      const p = JSON.parse(ev.data);
      const was = peersCache[p.id];
      peersCache[p.id] = {...p, last_msg: was ? was.last_msg : p.last_msg};
      if(isBoardPeer(p) && p.in_range && !boardOnline){ boardOnline = true; boardInfo = peersCache[p.id]; toast('⚡ Плата в эфире'); }
      renderSquad();
    }catch(_){}
  });
  es.addEventListener('leave', ev => { try{ onLeave(JSON.parse(ev.data).id); }catch(_){} });
  es.addEventListener('board', ev => {
    try{
      const b = JSON.parse(ev.data);
      const was = boardOnline;
      boardOnline = !!b.online; boardInfo = b.board || null;
      if(boardOnline !== was){
        if(boardOnline) toast('⚡ Плата в эфире');
        else toast('Плата пропала из эфира');
        renderSquad(); tick();
      }
    }catch(_){}
  });
  es.onerror = () => { sseLive = false; };
}

/* ---------- heartbeat: «я тут», как заявить о себе ---------- */
async function beat(){
  try{
    await jpost('/api/peers', {id:'web:'+myname(), name:myname(), client:'web'});
    store.set('name', myname());
  }catch(_){}
}
$('myname').addEventListener('change', beat);
$('announce').onclick = async () => { await beat(); tick(); toast('Вы в эфире как ' + myname()); };
setInterval(beat, 10000);

/* ---------- composer ---------- */
$('send').onsubmit = async e => {
  e.preventDefault();
  const inp = $('text'), text = inp.value.trim();
  if(!text) return;
  try{ await jpost('/api/messages', {from:myname(), text}); }catch(_){}
  inp.value = ''; tick();
};
$('toboard').onclick = async () => {
  const inp = $('text'), text = inp.value.trim();
  if(!text) return;
  if(!boardOnline){ inp.placeholder = 'плата не в эфире'; toast('Плата не в эфире — текст не уйдёт'); return; }
  try{ await jpost('/api/bridge/send', {from:myname(), text}); inp.value = ''; }
  catch(err){ inp.placeholder = 'плата недоступна'; toast('Не ушло: ' + (err && err.error || 'ошибка')); }
  tick();
};
/* Шаблон/эмоция: в эфир через плату, если она есть, иначе локально. */
async function sendCanned(i){
  const text = CANNED_RU[i];
  if(boardOnline){
    try{ await jpost('/api/bridge/send', {from:myname(), canned:i, text}); }
    catch(err){ toast('Не ушло: ' + (err && err.error || 'ошибка')); }
  } else {
    try{ await jpost('/api/messages', {from:myname(), text}); }catch(_){}
  }
  tick();
}
async function sendEmote(i){
  const label = EMOTES[i][1] + ' ' + EMOTES[i][2];
  if(boardOnline){
    try{ await jpost('/api/bridge/send', {from:myname(), emote:i, text:label}); }catch(err){ toast('Не ушло'); }
  } else {
    try{ await jpost('/api/emotions', {from:myname(), emote:EMOTES[i][0]}); }catch(_){}
  }
  emoteReact(EMOTES[i][0]); tick();
}
async function sendReaction(i){
  if(!boardOnline){ toast('Реакции уходят в эфир — плата не в эфире'); return; }
  await sendCanned(i);
}
$('react-like').onclick = () => sendReaction(48);
$('react-dislike').onclick = () => sendReaction(49);

/* Шаблоны BroWatch: зеркало CANNED_RU[] из прошивки (src/meshwords.cpp).
   Индексы совпадают с эфирными: строка N здесь = строка N на плате.
   48/49 — реакции LIKE/DISLIKE, в пикер не входят (как на плате). */
const CANNED_RU=["Уже еду.","Ты где?","У меня чисто.","Тут что-то есть.","Выхожу.","Ща вернусь.","Ага.","Не-а.","Может.","Жду у машины.","Смотри в оба.","Камера слева.","Тут камера Флок.","Замри.","Давай ко мне.","Сваливаю.","Пять минут.","Опаздываю.","Ха.","Найс.","Спасибо.","Перекус?","Хвост за тобой?","Ухожу в тень.","Копы впереди.","Дрон сверху.","Камера на номера.","Их уже двое.","Уже ушли.","Я в порядке.","Сюда не иди.","Разворачивайся.","За мной хвост.","Ты в порядке?","Ты на месте?","Говорить можешь?","Куда дальше?","Сколько их?","Подвезти?","Набери как сможешь.","Принял.","Уже делаю.","Пока нет.","Вас понял.","Сквач, отбой.","Мощно, если так.","Бип-буп.","Будь сквачем.","Понравилось.","Не понравилось."];
const CANNED_TABS=[["ПУТЬ",[0,4,5,9,14,15,16,17]],["ВИЖУ",[3,11,12,24,25,26,27,28]],["СТАТУС",[2,10,13,23,29,30,31,32]],["ВОПРОС",[1,22,33,34,35,36,37,38]],["ОТВЕТ",[6,7,8,20,39,40,41,42]],["СКВАЧ",[18,19,21,43,44,45,46,47]]];
let cannedTab = 0;
function drawCanned(){
  const tabs = $('canned-tabs');
  tabs.innerHTML = CANNED_TABS.map((t,i) =>
    `<button role="tab" data-t="${i}" class="${i===cannedTab?'on':''}">${esc(t[0])}</button>`).join('');
  tabs.querySelectorAll('button').forEach(b => b.onclick = () => { cannedTab = +b.dataset.t; drawCanned(); });
  const box = $('canned-lines');
  box.innerHTML = CANNED_TABS[cannedTab][1].map(i =>
    `<button data-i="${i}">${esc(CANNED_RU[i])}</button>`).join('');
  box.querySelectorAll('button').forEach(b => b.onclick = () => sendCanned(+b.dataset.i));
}
drawCanned();
$('btn-canned').onclick = () => {
  const p = $('canned-panel'), open = p.hidden;
  p.hidden = !open; $('emote-panel').hidden = true;
  $('btn-canned').setAttribute('aria-expanded', String(open));
  $('btn-emotes').setAttribute('aria-expanded', 'false');
};
/* Пикет эмоций платы: 6 табов TAB_NAME_RU, состав TAB (src/emote_script.cpp). */
let emoteTab = 0;
function drawEmotes(){
  const tabs = $('emote-tabs');
  tabs.innerHTML = EMOTE_TABS.map((t,i) =>
    `<button role="tab" data-t="${i}" class="${i===emoteTab?'on':''}">${esc(t[0])}</button>`).join('');
  tabs.querySelectorAll('button').forEach(b => b.onclick = () => { emoteTab = +b.dataset.t; drawEmotes(); });
  const box = $('emote-grid');
  box.innerHTML = EMOTE_TABS[emoteTab][1].map(i =>
    `<button data-i="${i}" title="${esc(EMOTES[i][2])}"><span>${EMOTES[i][1]}</span><small>${esc(EMOTES[i][2])}</small></button>`).join('');
  box.querySelectorAll('button').forEach(b => b.onclick = () => sendEmote(+b.dataset.i));
}
drawEmotes();
$('btn-emotes').onclick = () => {
  const p = $('emote-panel'), open = p.hidden;
  p.hidden = !open; $('canned-panel').hidden = true;
  $('btn-emotes').setAttribute('aria-expanded', String(open));
  $('btn-canned').setAttribute('aria-expanded', 'false');
};

/* ---------- PWA ---------- */
if('serviceWorker' in navigator){
  addEventListener('load', () => {
    navigator.serviceWorker.register('/sw.js').catch(() => {});
  });
}

document.addEventListener('visibilitychange', () => { if(!document.hidden){ beat(); tick(); } });
connectSSE();
setInterval(tick, 2000);
beat(); tick();
