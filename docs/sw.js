// Offline shell: the recorder opens (and records) without a connection; uploads wait until there is one.
const CACHE = 'wtrec-v2';
const FILES = ['./', 'index.html', 'app.js', 'wav.js', 'loudness.js', 'worklet.js', 'manifest.webmanifest', 'icon.png', 'icon-512.png'];

self.addEventListener('install', (e) => {
  e.waitUntil(caches.open(CACHE).then((c) => c.addAll(FILES)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', (e) => {
  e.waitUntil(caches.keys().then((keys) => Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k)))).then(() => self.clients.claim()));
});

self.addEventListener('fetch', (e) => {
  const url = new URL(e.request.url);
  if (url.origin !== location.origin) return; // Google sign-in and Drive always go to the network
  // network first, so a new version is picked up as soon as there is a connection
  e.respondWith(fetch(e.request).then((r) => {
    const copy = r.clone();
    caches.open(CACHE).then((c) => c.put(e.request, copy));
    return r;
  }).catch(() => caches.match(e.request)));
});
