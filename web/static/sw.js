/* BroWatch Web — service worker: app-shell в кэш, API всегда из сети. */
const CACHE = 'bw-0.2.1';
const SHELL = ['/', '/static/index.html', '/static/app.js', '/static/style.css',
  '/static/icon.svg', '/static/icon-192.png', '/static/icon-512.png',
  '/manifest.webmanifest'];

self.addEventListener('install', ev => {
  ev.waitUntil(caches.open(CACHE).then(c => c.addAll(SHELL)).then(() => self.skipWaiting()));
});
self.addEventListener('activate', ev => {
  ev.waitUntil(caches.keys()
    .then(keys => Promise.all(keys.filter(k => k !== CACHE).map(k => caches.delete(k))))
    .then(() => self.clients.claim()));
});
self.addEventListener('fetch', ev => {
  const u = new URL(ev.request.url);
  if(u.pathname.startsWith('/api/')) return;   // живые данные — только сеть, в кэш не кладём
  if(ev.request.method !== 'GET') return;
  ev.respondWith(
    caches.match(ev.request).then(hit => {
      const net = fetch(ev.request).then(res => {
        if(res && res.ok){ const copy = res.clone(); caches.open(CACHE).then(c => c.put(ev.request, copy)); }
        return res;
      }).catch(() => hit);
      return hit || net;
    })
  );
});
