'use strict';
/* BroWatch Web v0.2 — SquachWare UI, полный паритет с прошивкой.
   Маскот: LILGUY 1:1 из прошивки (include/lil_guy.h): 8 кадров 10x10,
   2 бита на пиксель, кадр = (now/80)%8. Цвета из Theme::drawLilGuy:
   hair #00FFF5, skin #FFD0F0, body #B967FF.
   ЛОГОВО — часы DESK (src/ui_desk.cpp): семисегментные цифры 1:1 (SEG),
   персонаж платы + сквад в их скинах (outfit/shade из моста), всплывающее
   ПИСЬМО как XP-коробка платы, карточка детекции, настройки — с платы.
   Фоны: подмножество Settings::Background. Цвета типов — Theme::colorFor,
   имена — TypeNames::ru.
   Шаблоны: CANNED_RU[] 0..49 = src/meshwords.cpp (48/49 — реакции
   LIKE/DISLIKE, шлются в эфир как canned). Табы шаблонов = CANNED_TAB.
   Эмоции: 35 штук MeshMsg::Emote (meshmsg.h), имена/табы/RU-ярлыки =
   EmoteScript NAME/NAME_RU/TAB (src/emote_script.cpp). Мост привозит имя
   (NAME[i][0]); индекс для отправки восстанавливаем по EMOTE_BY_NAME.
   Presence: пир «в эфире», пока свежак (плата держит 12 с, сервер — 15 с).
   У веба своей персоны НЕТ: всё сказанное уходит в эфир через плату от её
   персонажа (nick из announce платы, иначе имя устройства). Без платы
   в эфире отправка невозможна — молчание честнее выдуманного собеседника. */
const $ = id => document.getElementById(id);
const chatEl = $('chat'), peersBox = $('squad'), recentBox = $('recent'),
      detsBox = $('dets'), statusEl = $('status'), boardPill = $('board-name');

/* ---------- prefs ---------- */
const store = {
  get(k, d){ try{ const v = localStorage.getItem('bw_'+k); return v === null ? d : v; }catch(e){ return d; } },
  set(k, v){ try{ localStorage.setItem('bw_'+k, v); }catch(e){} }
};

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
function drawLilGuy(ctx, f, x, y, s, flip, pal){
  const P = pal || LIL_PAL;
  for(let yy = 0; yy < 10; yy++){
    const row = LILGUY[f * 10 + yy] >>> 0;
    for(let xx = 0; xx < 10; xx++){
      const c = (row >>> (xx * 2)) & 3;
      if(!c || !P[c]) continue;
      const dx = flip ? 9 - xx : xx;
      ctx.fillStyle = P[c];
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
    const me = m.via === 'board';   // своё — всё, что ушло через плату
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
    if(live && m.via !== 'board'){
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
    if(live && e.via !== 'board'){
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
    : '<div class="empty">Рядом никого. Члены отряда появятся сами, когда их услышит плата, — добавить вручную нельзя.</div>';
  recentBox.innerHTML = gone.length ? gone.map(squadRow).join('')
    : '<div class="empty">Все, кого слышали, до сих пор в эфире.</div>';
  $('range-count').textContent = here.length ? '· ' + here.length : '';
  const b = boardInfo;
  const pers = $('persona');
  if(boardOnline && b){
    $('board-pill').hidden = false; boardPill.textContent = b.name || b.id;
    pers.innerHTML = `Вы пишете как <b>${esc(b.nick || b.name || b.id)}</b> — персонаж платы`;
    const bc = $('board-card'); bc.hidden = false;
    bc.innerHTML = `<div class="top"><span class="dot in"></span><span class="nm">⚡ ${esc(b.name || b.id)}</span>`+
      `<span class="tag squad">плата</span></div><div class="last">${esc(b.client || '')} · язык ${esc(b.lang || '?')}</div>`;
  } else {
    $('board-pill').hidden = true; $('board-card').hidden = true;
    pers.innerHTML = 'Плата не в эфире — подключите её по USB и разблокируйте PIN';
  }
  $('foot-board').textContent = 'плата: ' + (boardOnline && b ? (b.name || b.id) : 'не в эфире');
  renderDenSettings();
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

/* ================= ЛОГОВО: часы DESK с платы (src/ui_desk.cpp) =================
   Часы семисегментные 1:1 (SEG-биты оттуда), персонаж платы в её скине
   (outfit/shade из announce) + сквад в своих скинах, всплывающее ПИСЬМО
   как XP-коробка платы, карточка детекции, местный таймер ФОКУС/ПЕРЕРЫВ.
   Настройки (сквад вкл/выкл, сколько тел, часы, фон) — с платы, зеркалятся
   в полоске над сценой; меняются на плате (DESK MODE), здесь только чтение.
   Без платы — часы идут, сквада и настроек нет: молчание честнее выдумки. */
const SEG = [0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F];
/* Скины: [акцент, морда, тело] по индексу аутфита прошивки
   (NONE..SHARK, общие таблицы — 4 бита в рекламе). Морда одна на всех:
   вид один, костюмы разные. Цвета тела — с оглядкой на squachy.cpp
   (VOIDEYE/парка/тень — оттуда буквально). */
const OUTFIT_COLS = [
  ['#00fff5','#ffd0f0','#8a5a33'],['#5a3a1a','#ffd0f0','#7a4a22'],
  ['#ff71ce','#ffd0f0','#aeddff'],['#ffffff','#ffd0f0','#c0c0c8'],
  ['#3a3a4a','#c8a0d8','#14141c'],['#e03030','#ffd0f0','#2a5ad0'],
  ['#00cc44','#ffd0f0','#2a5ad0'],['#c0c0ff','#ffd0f0','#2a2a4a'],
  ['#ffffff','#ffd0f0','#2266ff'],['#f4c20d','#ffd0f0','#2040c0'],
  ['#aaaaaa','#ffd0f0','#5a5a62'],['#ffffff','#ffd0f0','#d8d8e8'],
  ['#8a80e0','#ffd0f0','#302c60'],['#ffcf70','#ffd0f0','#ff8a1a'],
  ['#e8f0f4','#ffd0f0','#5a7a8c']];
const SHADE_COLS = ['#00fff5','#ff2d78','#00ff88','#b967ff'];  /* CYAN/PINK/GREEN/PURPLE */
/* Фон платы (Settings::Background) -> фон сцены. Неточных (аквариум, огонь,
   терминал, туннель) среди веб-фонов нет — едут на ближайший по духу. */
const DEN_BG = {0:'digital',1:'starfield',2:'toasters',3:'starfield',4:'digital',
  5:'fireflies',6:'starfield',7:'snowfall',8:'spectrum',9:'starfield',10:'synthwave',11:'black'};
const DEN_BG_RU = ['ЦИФРОВОЙ ДОЖДЬ','ЗВЁЗДЫ','ТОСТЕРЫ','АКВАРИУМ','ТЕРМИНАЛ','СВЕТЛЯЧКИ',
  'ОГОНЬ','СНЕГ','ГИБСОН','ТУННЕЛЬ','СИНТВЕЙВ','ЧЁРНЫЙ'];
const DEN_CLK_K = [0.78, 1.0, 1.3];  /* CLOCK_K платы */
const denCanvas = $('den'), dctx = denCanvas.getContext('2d');
let denSeenMsg = 0, denLastDraw = 0, denParts = [], denBgKey = '';
const denDesk = () => (boardOnline && boardInfo && boardInfo.desk) || null;

function denOutfitPal(o){
  const c = OUTFIT_COLS[(o|0)] || OUTFIT_COLS[0];
  return [null, c[0], c[1], c[2]];
}
function segDigit(ctx, d, x, y, w, h, th, col){
  const s = (d >= 0 && d <= 9) ? SEG[d] : 0;
  const mid = y + h/2 - th/2;
  const segs = [[1,x+th+1,y,w-2*th-2,th],[2,x+w-th,y+th+1,th,h/2-th-2],
    [4,x+w-th,mid+th+1,th,h/2-th-2],[8,x+th+1,y+h-th,w-2*th-2,th],
    [16,x,mid+th+1,th,h/2-th-2],[32,x,y+th+1,th,h/2-th-2],[64,x+th+1,mid,w-2*th-2,th]];
  ctx.fillStyle = col;
  for(const [bit,sx,sy,sw,sh] of segs) if(s & bit) ctx.fillRect(sx|0, sy|0, sw|0, sh|0);
}
function denWrap(ctx, text, maxW){
  const words = String(text).split(/\s+/).filter(Boolean), lines = [];
  let cur = '';
  for(const w of words){
    const t = cur ? cur + ' ' + w : w;
    if(ctx.measureText(t).width > maxW && cur){ lines.push(cur); cur = w; }
    else cur = t;
    if(lines.length === 2){ cur = cur.slice(0, 26) + '…'; break; }
  }
  if(cur) lines.push(cur);
  return lines.slice(0, 3);
}
function denSize(){
  const r = denCanvas.getBoundingClientRect();
  const d = Math.min(devicePixelRatio || 1, 2);
  const W = Math.max(200, Math.round(r.width)), H = Math.round(r.height) || 340;
  if(denCanvas.width !== Math.round(W*d) || denCanvas.height !== Math.round(H*d)){
    denCanvas.width = Math.round(W*d); denCanvas.height = Math.round(H*d);
    dctx.setTransform(d, 0, 0, d, 0, 0);
  }
  return [W, H];
}
/* Упрощённые фоны сцены — те же ключи, что у страницы, но частицы свои,
   чтобы сцена жила, даже когда у страницы выбран другой фон. */
function denBgInit(key, W, H){
  denParts = []; denBgKey = key;
  if(key === 'starfield') for(let i = 0; i < 90; i++) denParts.push({x:rnd(0,W), y:rnd(0,H), v:rnd(.1,.6), r:rnd(.5,1.6)});
  else if(key === 'snowfall') for(let i = 0; i < 70; i++) denParts.push({x:rnd(0,W), y:rnd(0,H), v:rnd(.4,1.4), r:rnd(1,3), ph:rnd(0,6)});
  else if(key === 'digital'){ const c = Math.floor(W/16); for(let i = 0; i < c; i++) denParts.push({x:i*16, y:rnd(-H,0), v:rnd(2,6)}); }
  else if(key === 'fireflies') for(let i = 0; i < 26; i++) denParts.push({x:rnd(0,W), y:rnd(0,H), vx:rnd(-.3,.3), vy:rnd(-.25,.25), ph:rnd(0,6)});
  else if(key === 'toasters') for(let i = 0; i < 4; i++) denParts.push({x:rnd(0,W), y:rnd(0,H*.6), v:rnd(.3,1), s:rnd(10,22)});
}
let denT = 0;
function denBgDraw(key, W, H){
  denT++;
  if(key === 'black'){ dctx.fillStyle = '#0a000f'; dctx.fillRect(0,0,W,H); return; }
  const g0 = dctx.createLinearGradient(0,0,0,H);
  g0.addColorStop(0,'#0a000f'); g0.addColorStop(1,'#160a24');
  dctx.fillStyle = g0; dctx.fillRect(0,0,W,H);
  if(key === 'starfield'){
    for(const p of denParts){ dctx.fillStyle = '#ffffffaa'; dctx.fillRect(p.x, p.y, p.r, p.r); p.x -= p.v; if(p.x < 0){ p.x = W; p.y = rnd(0,H); } }
  }else if(key === 'snowfall'){
    for(const p of denParts){ dctx.fillStyle = '#a0dcffcc'; dctx.beginPath(); dctx.arc(p.x + Math.sin(denT/40+p.ph)*10, p.y, p.r, 0, 7); dctx.fill(); p.y += p.v; if(p.y > H+5){ p.y = -5; p.x = rnd(0,W); } }
  }else if(key === 'digital'){
    dctx.font = '13px monospace';
    for(const p of denParts){ dctx.fillStyle = '#00ff8844'; dctx.fillText(GLYPHS[(Math.random()*GLYPHS.length)|0], p.x, p.y); p.y += p.v; if(p.y > H+20){ p.y = rnd(-80,-10); p.v = rnd(2,6); } }
  }else if(key === 'fireflies'){
    for(const p of denParts){ const a = .3 + .3*Math.sin(denT/30+p.ph); dctx.fillStyle = `rgba(200,255,0,${a})`; dctx.beginPath(); dctx.arc(p.x, p.y, 2.2, 0, 7); dctx.fill(); p.x += p.vx; p.y += p.vy; if(p.x<0)p.x=W; if(p.x>W)p.x=0; if(p.y<0)p.y=H; if(p.y>H)p.y=0; }
  }else if(key === 'toasters'){
    for(const p of denParts){ dctx.fillStyle = '#c8a24bbb'; dctx.beginPath(); dctx.roundRect(p.x, p.y, p.s*2, p.s, 4); dctx.fill(); p.x -= p.v; if(p.x < -p.s*2){ p.x = W+10; p.y = rnd(0,H*.6); } }
  }else if(key === 'synthwave'){
    const g = dctx.createLinearGradient(0,0,0,H);
    g.addColorStop(0,'#0a000f'); g.addColorStop(.5,'#2a0a3a'); g.addColorStop(.58,'#ff2d78'); g.addColorStop(.62,'#0a000f');
    dctx.fillStyle = g; dctx.fillRect(0,0,W,H);
    const sx=W/2, sr=Math.min(W,H)*.13, sy=H*.42;
    const sg = dctx.createLinearGradient(0,sy-sr,0,sy+sr);
    sg.addColorStop(0,'#fffb96'); sg.addColorStop(1,'#ff2d78');
    dctx.fillStyle = sg; dctx.beginPath(); dctx.arc(sx,sy,sr,0,7); dctx.fill();
  }else if(key === 'spectrum'){
    const n = 24, bw = W/n;
    for(let i = 0; i < n; i++){
      const d = detsCache[(detsCache.length-1-i+48) % Math.max(detsCache.length,1)];
      const h = detsCache.length ? Math.min(H*.5, Math.max(5,(100 + (d?d.rssi:-100)) * H/160)) : 4;
      dctx.fillStyle = d ? typeColor(d.type) : '#333355';
      dctx.fillRect(i*bw+1, H-h-24, Math.max(2,bw-2), h);
    }
  }else{
    for(const p of denParts){ dctx.fillStyle = '#ffffff88'; dctx.fillRect(p.x, p.y, 1.4, 1.4); p.x -= .3; if(p.x < 0) p.x = W; }
    if(!denParts.length) denBgInit('starfield', W, H);
  }
  dctx.fillStyle = '#0a000f55'; dctx.fillRect(0,0,W,H);
}
/* Свежее входящее письмо mesh (как inbox платы): всплывает XP-коробкой. */
function denLetter(){
  const t = Date.now()/1000;
  let best = null;
  for(const m of msgsCache){
    if(m.via !== 'mesh' || typeof m.id !== 'number') continue;
    if(!m.text || !String(m.text).trim()) continue;
    if(m.id === denSeenMsg || t - m.ts > 20) continue;
    if(!best || m.ts > best.ts) best = m;
  }
  return best;
}
denCanvas.onclick = () => { const l = denLetter(); if(l) denSeenMsg = l.id; };
function denFrame(){
  requestAnimationFrame(denFrame);
  if(document.hidden) return;
  if(!DESKTOP.matches && activeTab !== 'den') return;
  const t = performance.now();
  if(!motionOn && t - denLastDraw < 1000) return;
  denLastDraw = t;
  const [W, H] = denSize();
  const desk = denDesk();
  const bgKey = DEN_BG[desk ? desk.bg : -1] || 'starfield';
  if(bgKey !== denBgKey) denBgInit(bgKey, W, H);
  denBgDraw(bgKey, W, H);
  const nowMs = Date.now(), now = new Date(nowMs);
  const k = DEN_CLK_K[(desk && desk.clk <= 2) ? desk.clk : 1] || 1;
  const bangers = !!(desk && desk.clkfont === 1);

  /* --- плашка часов, как plate платы: дата сверху, время крупно --- */
  const dh = 40*k, dw = 27*k, th = Math.max(3, 6*k), gap = 5*k, colonW = 9*k;
  const hh = now.getHours(), h12 = hh % 12 || 12, mm = now.getMinutes();
  const dig = String(h12) + ':' + String(mm).padStart(2,'0');
  const cells = dig.replace(':','').length;
  const digitsW = cells*dw + (cells-1)*gap + colonW + 30*k;
  const pw = Math.min(W - 12, digitsW + 84*k), px = (W - pw)/2, py = 6;
  const dateStr = now.toLocaleDateString('ru-RU', {weekday:'short', day:'numeric', month:'long'});
  dctx.font = '11px system-ui,sans-serif';
  const dateW = dctx.measureText(dateStr).width;
  const ph = py + 20 + dh + 10;
  dctx.fillStyle = 'rgba(10,0,15,.82)';
  dctx.beginPath(); dctx.roundRect(px, py, pw, ph, 8); dctx.fill();
  dctx.strokeStyle = '#b967ff'; dctx.lineWidth = 1.5;
  dctx.beginPath(); dctx.roundRect(px, py, pw, ph, 8); dctx.stroke();
  dctx.fillStyle = '#00fff5'; dctx.textAlign = 'center'; dctx.textBaseline = 'alphabetic';
  dctx.fillText(dateStr, W/2, py + 15, Math.max(dateW, pw - 12));
  let cx = (W - digitsW)/2 + inkLead(dig[0], dw, th);
  const cy = py + 22, col = '#ff71ce', blink = (nowMs/500|0) % 2 === 0;
  if(bangers){
    dctx.fillStyle = col;
    dctx.font = `900 ${Math.round(dh)}px Impact,'Arial Black',sans-serif`;
    dctx.fillText(dig, W/2, cy + dh*.82);
    cx = W/2 + digitsW/2;
  }else{
    let xi = 0;
    const ds = dig.replace(':','');
    for(let i = 0; i < ds.length; i++){
      segDigit(dctx, +ds[i], cx + xi, cy, dw, dh, th, col);
      xi += dw + gap;
      if(i === (h12 > 9 ? 1 : 0)){
        if(blink){ dctx.fillStyle = col; dctx.fillRect(cx+xi, cy+dh/3-2, 4, 4); dctx.fillRect(cx+xi, cy+2*dh/3-2, 4, 4); }
        xi += colonW;
      }
    }
    cx += xi;
  }
  dctx.fillStyle = '#fffb96'; dctx.font = 'bold 11px system-ui,sans-serif'; dctx.textAlign = 'left';
  dctx.fillText(hh < 12 ? 'AM' : 'PM', Math.min(cx + 4, W - 30), cy + dh - 2);
  const plateBottom = py + ph;

  /* --- письмо: XP-коробка под плашкой --- */
  const letter = denLetter();
  if(letter){
    const bw = Math.min(W - 16, 300);
    const bx = (W - bw)/2;
    dctx.font = '12px system-ui,sans-serif';
    const rows = denWrap(dctx, letter.text, bw - 16);
    const bh = 15 + 6 + rows.length*15 + 16;
    const by = plateBottom + 4;
    dctx.fillStyle = '#0A5FE6';
    dctx.beginPath(); dctx.roundRect(bx, by, bw, bh, 4); dctx.fill();
    dctx.fillStyle = '#3D95FF'; dctx.fillRect(bx+1, by+2, bw-2, 3);
    dctx.fillStyle = '#fff'; dctx.font = 'bold 10px system-ui,sans-serif'; dctx.textAlign = 'left';
    dctx.fillText('✉ ПИСЬМО · ' + String(letter.from).slice(0, 14), bx + 6, by + 12);
    dctx.fillStyle = '#D65434';
    dctx.beginPath(); dctx.roundRect(bx+bw-19, by+2, 14, 11, 2); dctx.fill();
    dctx.fillStyle = '#fff'; dctx.fillText('x', bx+bw-15, by + 11);
    dctx.fillStyle = '#ECE9D8'; dctx.fillRect(bx+2, by+15, bw-4, bh-17);
    dctx.fillStyle = '#000'; dctx.font = '12px system-ui,sans-serif';
    rows.forEach((r, i) => dctx.fillText(r, bx+8, by+15+16+i*15));
    dctx.fillStyle = '#D65434'; dctx.font = '10px system-ui,sans-serif'; dctx.textAlign = 'right';
    dctx.fillText(tHHMM(letter.ts), bx+bw-6, by+bh-5);
    dctx.textAlign = 'left';
  }

  /* --- сквад: плата по центру, гости рядом, каждый в своём скине --- */
  const floorY = H - 34;
  const frame = Math.floor(t/80) % 8;
  const label = (x, y, text, dotCol, bold) => {
    dctx.font = (bold ? 'bold ' : '') + '11px system-ui,sans-serif';
    const tw = Math.min(dctx.measureText(text).width, 110);
    dctx.fillStyle = 'rgba(10,0,15,.7)';
    const lx = x - tw/2 - 8, ly = y - 2;
    dctx.fillRect(lx, ly, tw + 16, 15);
    if(dotCol){ dctx.fillStyle = dotCol; dctx.beginPath(); dctx.arc(lx + 7, ly + 7.5, 3, 0, 7); dctx.fill(); }
    dctx.fillStyle = bold ? '#00fff5' : '#f2eef6';
    dctx.textAlign = 'center';
    dctx.fillText(text, x, y + 10, 110);
    dctx.textAlign = 'left';
  };
  const bubble = (x, y, text) => {
    dctx.font = '11px system-ui,sans-serif';
    const rows = denWrap(dctx, text, 130);
    const bw2 = Math.min(140, Math.max(...rows.map(r => dctx.measureText(r).width)) + 14);
    const bh2 = rows.length*14 + 10;
    const bx2 = Math.max(4, Math.min(W - bw2 - 4, x - bw2/2)), by2 = Math.max(plateBottom + 2, y - bh2 - 8);
    dctx.fillStyle = '#f2eef6';
    dctx.beginPath(); dctx.roundRect(bx2, by2, bw2, bh2, 7); dctx.fill();
    dctx.fillStyle = '#0a000f';
    rows.forEach((r, i) => dctx.fillText(r, bx2 + 7, by2 + 14 + i*14));
    dctx.beginPath();
    dctx.moveTo(x - 4, by2 + bh2); dctx.lineTo(x + 4, by2 + bh2); dctx.lineTo(x, by2 + bh2 + 6);
    dctx.fill();
  };
  const drawMember = (x, y, s, o, fr, flip) => {
    drawLilGuy(dctx, fr, x - 5*s, y - 10*s, s, flip, denOutfitPal(o));
  };
  const members = Object.values(peersCache)
    .filter(p => p.in_range && !isBoardPeer(p))
    .sort((a, b) => (a.id < b.id ? -1 : 1));
  const crowdN = desk ? Math.min(Math.max(desk.crowd|0, 1), 8) : 4;
  const showN = desk && !desk.squad ? 0 : Math.min(members.length, Math.max(crowdN - 1, 1));
  const shown = members.slice(0, showN);
  if(!boardOnline){
    dctx.fillStyle = '#a89bb5'; dctx.font = '13px system-ui,sans-serif'; dctx.textAlign = 'center';
    dctx.fillText('плата не в эфире — сквад и настройки появятся при подключении', W/2, plateBottom + 24, W - 20);
    dctx.textAlign = 'left';
  }else{
    const b = boardInfo || {};
    const bs = 6, bx = W/2, by = floorY;
    const bob = motionOn ? Math.sin(t/320)*2 : 0;
    drawMember(bx, by + bob, bs, b.outfit, frame, false);
    const bname = b.nick || b.name || b.id || 'плата';
    label(bx, by + 6, bname, SHADE_COLS[b.shade|0] || SHADE_COLS[0], true);
    const n = shown.length, ms = n > 4 ? 4 : 5;
    shown.forEach((p, i) => {
      const gx = n === 0 ? bx : 30 + (W - 60) * (n === 1 ? (p.id < b.id ? 0.12 : 0.88) : i/(n - 1 || 1));
      const bob2 = motionOn ? Math.sin(t/300 + i*1.7)*2.5 : 0;
      const fr2 = (frame + i*2) % 8;
      drawMember(gx, floorY + bob2, ms, p.outfit, fr2, gx > bx);
      const nm = p.nick || p.name || p.id;
      label(gx, floorY + 6, String(nm).slice(0, 12), SHADE_COLS[p.shade|0] || null, false);
      const lm = p.last_msg;
      if(lm && lm.text && String(lm.text).trim() && (Date.now()/1000 - lm.ts) < 600)
        bubble(gx, floorY - 10*ms + bob2, lm.text);
    });
    if(desk && !desk.squad){
      dctx.fillStyle = '#a89bb5'; dctx.font = '12px system-ui,sans-serif'; dctx.textAlign = 'center';
      dctx.fillText('сквад выключен на плате (DESK MODE)', W/2, H - 8);
      dctx.textAlign = 'left';
    }
  }

  /* --- карточка детекции, как alert платы: свежая — в углу --- */
  const dt = Date.now()/1000;
  const last = detsCache.length ? detsCache[detsCache.length-1] : null;
  if(last && dt - last.ts < 20){
    const cw = 150, chh = 40, dx = 6, dy = H - chh - 6;
    dctx.fillStyle = 'rgba(10,0,15,.85)';
    dctx.beginPath(); dctx.roundRect(dx, dy, cw, chh, 6); dctx.fill();
    dctx.strokeStyle = typeColor(last.type || ''); dctx.lineWidth = 2;
    dctx.beginPath(); dctx.roundRect(dx, dy, cw, chh, 6); dctx.stroke();
    dctx.fillStyle = typeColor(last.type || ''); dctx.font = 'bold 11px system-ui,sans-serif'; dctx.textAlign = 'left';
    dctx.fillText('📡 ' + (TYPE_RU[last.type] || last.type || '?'), dx + 8, dy + 15);
    dctx.fillStyle = '#a89bb5'; dctx.font = '10px system-ui,sans-serif';
    dctx.fillText(String(last.vendor || last.mac || '') + ' · ' + (last.rssi ?? ''), dx + 8, dy + 30);
  }
}
/* Отступ первой чернильной цифры, как на плате: «1» светит только правыми
   сегментами, и центровка по ячейкам увела бы время вправо. */
function inkLead(ch, dw, th){ return ch === '1' ? dw - th : 0; }
function renderDenSettings(){
  const el = $('den-settings');
  const d = denDesk();
  if(!boardOnline || !d){ el.innerHTML = 'Логово зеркалит часы платы. <b>Плата не в эфире</b> — подключите её по USB.'; return; }
  const crowd = (d.crowd|0) <= 1 ? 'ОДИН' : 'ДО ' + (d.crowd|0);
  const clkN = ['мелкие', 'средние', 'крупные'][(d.clk|0)] || 'средние';
  const bgN = DEN_BG_RU[d.bg] || ('фон ' + d.bg);
  el.innerHTML = `⚙ платы: сквад <b>${d.squad ? 'вкл' : 'выкл'}</b> · тел <b>${crowd}</b>` +
    ` · визит <b>${d.visit ? 'полный' : 'рядом'}</b> · часы <b>${clkN}${d.clkfont ? ', Bangers' : ', сегменты'}</b>` +
    ` · фон <b>${esc(bgN)}</b> — меняются на плате (DESK MODE)`;
}
requestAnimationFrame(denFrame);

/* ---------- местный таймер ФОКУС/ПЕРЕРЫВ (плату не трогает) ---------- */
let denTimer = null;
function denTimerLabel(){
  const b = $('den-timer');
  if(!denTimer){ b.textContent = '⏱ ФОКУС 25'; b.classList.remove('run'); return; }
  const left = Math.max(0, Math.round((denTimer.end - Date.now())/1000));
  const what = denTimer.phase === 'focus' ? 'ФОКУС' : 'ПЕРЕРЫВ';
  b.textContent = `⏱ ${what} ${Math.floor(left/60)}:${String(left%60).padStart(2,'0')}`;
  b.classList.add('run');
}
$('den-timer').onclick = () => {
  if(denTimer){ denTimer = null; toast('Таймер выкл. Без осуждения.'); }
  else { denTimer = {phase:'focus', end: Date.now() + 25*60*1000}; toast('Двадцать пять минут. Время веду я.'); }
  denTimerLabel();
};
setInterval(() => {
  if(!denTimer) return;
  if(Date.now() >= denTimer.end){
    if(denTimer.phase === 'focus'){ denTimer = {phase:'break', end: Date.now() + 5*60*1000}; toast('Время. Встань, посмотри вдаль. Пять минут.'); }
    else { denTimer = null; toast('Перерыв окончен. За дело.'); }
  }
  denTimerLabel();
}, 1000);

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

/* ---------- composer: всё — в эфир через плату, от её персонажа ---------- */
$('send').onsubmit = async e => {
  e.preventDefault();
  const inp = $('text'), text = inp.value.trim();
  if(!text) return;
  if(!boardOnline){ inp.placeholder = 'плата не в эфире'; toast('Плата не в эфире — текст не уйдёт'); return; }
  try{ await jpost('/api/bridge/send', {text}); inp.value = ''; }
  catch(err){ toast('Не ушло: ' + (err && err.error || 'ошибка')); }
  tick();
};
/* Шаблон/эмоция: только через плату. Без платы — молчим, а не выдумываем. */
async function sendCanned(i){
  if(!boardOnline){ toast('Плата не в эфире — шаблон не уйдёт'); return; }
  try{ await jpost('/api/bridge/send', {canned:i, text:CANNED_RU[i]}); }
  catch(err){ toast('Не ушло: ' + (err && err.error || 'ошибка')); }
  tick();
}
async function sendEmote(i){
  if(!boardOnline){ toast('Плата не в эфире — эмоция не уйдёт'); return; }
  try{ await jpost('/api/bridge/send', {emote:i, text:EMOTES[i][1] + ' ' + EMOTES[i][2]}); }
  catch(err){ toast('Не ушло: ' + (err && err.error || 'ошибка')); }
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

document.addEventListener('visibilitychange', () => { if(!document.hidden) tick(); });
connectSSE();
setInterval(tick, 2000);
tick();
