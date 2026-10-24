// WaveTagger Rec: records 48 kHz / 24 bit WAV in the browser and uploads it to a Google Drive folder.
(function () {
  'use strict';

  var $ = function (id) { return document.getElementById(id); };
  var SCOPE = 'https://www.googleapis.com/auth/drive';
  var TARGET_RATE = 48000;

  // ---------------------------------------------------------------- settings
  function loadSettings() {
    try { return JSON.parse(localStorage.getItem('wtrec.settings') || '{}'); } catch (e) { return {}; }
  }
  function saveSettings(s) {
    try { localStorage.setItem('wtrec.settings', JSON.stringify(s)); } catch (e) { /* private mode */ }
  }
  function folderIdOf(text) {
    var t = (text || '').trim();
    var m = t.match(/folders\/([A-Za-z0-9_-]+)/) || t.match(/[?&]id=([A-Za-z0-9_-]+)/);
    return m ? m[1] : t;
  }

  // ---------------------------------------------------------------- storage (recordings wait here until they are uploaded)
  var dbp = null;
  function db() {
    if (!dbp) {
      dbp = new Promise(function (resolve, reject) {
        var r = indexedDB.open('wtrec', 1);
        r.onupgradeneeded = function () { r.result.createObjectStore('rec', { keyPath: 'id' }); };
        r.onsuccess = function () { resolve(r.result); };
        r.onerror = function () { reject(r.error); };
      });
    }
    return dbp;
  }
  function tx(mode, fn) {
    return db().then(function (d) {
      return new Promise(function (resolve, reject) {
        var t = d.transaction('rec', mode);
        var out = fn(t.objectStore('rec'));
        t.oncomplete = function () { resolve(out && out.result !== undefined ? out.result : undefined); };
        t.onerror = function () { reject(t.error); };
      });
    });
  }
  function putRec(rec) { return tx('readwrite', function (s) { return s.put(rec); }); }
  function allRecs() {
    return db().then(function (d) {
      return new Promise(function (resolve, reject) {
        var r = d.transaction('rec', 'readonly').objectStore('rec').getAll();
        r.onsuccess = function () { resolve(r.result.sort(function (a, b) { return b.createdAt - a.createdAt; })); };
        r.onerror = function () { reject(r.error); };
      });
    });
  }

  // ---------------------------------------------------------------- Google sign-in (access token only; nothing is stored on a server)
  var token = null, tokenExpires = 0, tokenClient = null;
  try {
    var saved = JSON.parse(sessionStorage.getItem('wtrec.token') || 'null');
    if (saved && saved.exp > Date.now()) { token = saved.token; tokenExpires = saved.exp; }
  } catch (e) { /* ignore */ }

  function tokenValid() { return token && Date.now() < tokenExpires - 60000; }

  function loadGsi() {
    return new Promise(function (resolve, reject) {
      if (window.google && google.accounts && google.accounts.oauth2) return resolve();
      var s = document.createElement('script');
      s.src = 'https://accounts.google.com/gsi/client';
      s.onload = function () { resolve(); };
      s.onerror = function () { reject(new Error('Google のログイン部品を読み込めません (オフライン?)')); };
      document.head.appendChild(s);
    });
  }

  // Must be called from a tap (it may open the Google pop-up).
  function requestToken() {
    var settings = loadSettings();
    if (!settings.clientId) return Promise.reject(new Error('設定で OAuth クライアント ID を入れてください'));
    return loadGsi().then(function () {
      return new Promise(function (resolve, reject) {
        tokenClient = google.accounts.oauth2.initTokenClient({
          client_id: settings.clientId,
          scope: SCOPE,
          callback: function (resp) {
            if (resp.error) return reject(new Error(resp.error_description || resp.error));
            token = resp.access_token;
            tokenExpires = Date.now() + (resp.expires_in || 3600) * 1000;
            try { sessionStorage.setItem('wtrec.token', JSON.stringify({ token: token, exp: tokenExpires })); } catch (e) { /* ignore */ }
            showAuth();
            resolve();
          },
          error_callback: function (err) { reject(new Error(err && err.type ? err.type : 'ログインが中断されました')); }
        });
        tokenClient.requestAccessToken({ prompt: '' });
      });
    });
  }

  function showAuth() {
    $('auth').textContent = tokenValid() ? '接続済み (約 1 時間有効)' : '未接続';
  }

  // ---------------------------------------------------------------- upload (resumable, one request per file)
  function uploadRec(rec) {
    var settings = loadSettings();
    var folder = folderIdOf(settings.folder);
    if (!folder) return Promise.reject(new Error('設定で保存先フォルダを入れてください'));
    if (!tokenValid()) return Promise.reject(new Error('Google に接続していません'));

    return fetch('https://www.googleapis.com/upload/drive/v3/files?uploadType=resumable&supportsAllDrives=true', {
      method: 'POST',
      headers: {
        'Authorization': 'Bearer ' + token,
        'Content-Type': 'application/json; charset=UTF-8',
        'X-Upload-Content-Type': 'audio/wav',
        'X-Upload-Content-Length': String(rec.blob.size)
      },
      body: JSON.stringify({ name: rec.name, parents: [folder] })
    }).then(function (r) {
      if (!r.ok) return r.text().then(function (t) { throw new Error('開始に失敗 (' + r.status + ') ' + t.slice(0, 120)); });
      var url = r.headers.get('Location');
      if (!url) throw new Error('アップロード先が得られませんでした');
      return fetch(url, { method: 'PUT', headers: { 'Content-Type': 'audio/wav' }, body: rec.blob });
    }).then(function (r) {
      if (!r.ok) return r.text().then(function (t) { throw new Error('送信に失敗 (' + r.status + ') ' + t.slice(0, 120)); });
    });
  }

  var uploading = false;
  function uploadPending() {
    if (uploading) return Promise.resolve();
    uploading = true;
    return allRecs().then(function (recs) {
      var pending = recs.filter(function (r) { return r.status !== 'done'; }).reverse();
      var chain = Promise.resolve();
      pending.forEach(function (rec) {
        chain = chain.then(function () {
          if (!tokenValid()) { return; }
          return uploadRec(rec).then(function () {
            rec.status = 'done'; rec.error = '';
            rec.blob = null; // the file is in Drive: free the space
            return putRec(rec);
          }).catch(function (e) {
            rec.status = 'fail'; rec.error = String(e.message || e);
            return putRec(rec);
          }).then(renderList);
        });
      });
      return chain;
    }).then(function () { uploading = false; return renderList(); }, function (e) { uploading = false; throw e; });
  }

  // ---------------------------------------------------------------- recording
  var ctx = null, stream = null, node = null, source = null, chunks = [], channels = 1, rate = TARGET_RATE;
  var recording = false, startedAt = 0, timer = null, wake = null, level = 0, peakPos = 0, peakAt = 0;

  function pad(n) { return (n < 10 ? '0' : '') + n; }
  function stamp(d) {
    return d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate()) + '_' + pad(d.getHours()) + '-' + pad(d.getMinutes()) + '-' + pad(d.getSeconds());
  }

  function startRecording() {
    var constraints = { audio: { channelCount: { ideal: 2 }, sampleRate: { ideal: TARGET_RATE }, echoCancellation: false, noiseSuppression: false, autoGainControl: false } };
    return navigator.mediaDevices.getUserMedia(constraints).then(function (s) {
      stream = s;
      ctx = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: TARGET_RATE });
      rate = ctx.sampleRate;
      return ctx.audioWorklet.addModule('worklet.js');
    }).then(function () {
      source = ctx.createMediaStreamSource(stream);
      var count = (stream.getAudioTracks()[0].getSettings().channelCount) || 1;
      channels = Math.min(2, Math.max(1, count));
      node = new AudioWorkletNode(ctx, 'recorder', { channelCount: channels, channelCountMode: 'explicit', numberOfOutputs: 0 });
      chunks = [];
      node.port.onmessage = function (e) {
        if (e.data === 'flushed' || !Array.isArray(e.data)) return;
        chunks.push(e.data);
        level = WavEnc.peakOf(e.data);
      };
      source.connect(node);
      recording = true;
      startedAt = Date.now();
      if (navigator.wakeLock) navigator.wakeLock.request('screen').then(function (l) { wake = l; }, function () {});
      tick();
      timer = setInterval(tick, 100);
      $('rec').textContent = '停止';
      $('rec').classList.add('on');
      $('info').textContent = rate + ' Hz / 24 bit / ' + (channels === 2 ? 'ステレオ' : 'モノラル') + (rate !== TARGET_RATE ? ' (端末が 48 kHz に対応していません)' : '');
    });
  }

  function stopRecording() {
    recording = false;
    clearInterval(timer);
    $('rec').classList.remove('on');
    $('rec').textContent = '録音';
    $('info').textContent = '保存中...';

    // the pop-up has to open from this tap, so ask for the token right now if there is none
    var auth = tokenValid() ? Promise.resolve() : requestToken().catch(function () { /* the file waits for a later tap */ });

    return new Promise(function (resolve) {
      var done = false;
      var finish = function () { if (!done) { done = true; resolve(); } };
      node.port.onmessage = function (e) {
        if (Array.isArray(e.data)) { chunks.push(e.data); return; }
        if (e.data === 'flushed') finish();
      };
      node.port.postMessage('flush');
      setTimeout(finish, 500);
    }).then(function () {
      source.disconnect();
      stream.getTracks().forEach(function (t) { t.stop(); });
      ctx.close();
      if (wake) { wake.release(); wake = null; }

      var enc = WavEnc.encodeWav24(chunks, channels, rate);
      chunks = [];
      var blob = new Blob(enc.parts, { type: 'audio/wav' });
      var name = stamp(new Date(startedAt)) + '.wav';
      return putRec({ id: name, name: name, blob: blob, size: blob.size, status: 'wait', error: '', createdAt: startedAt, seconds: enc.frames / rate })
        .then(function () { $('info').textContent = '保存しました: ' + name; return renderList(); });
    }).then(function () { return auth; }).then(function () { return uploadPending(); });
  }

  function tick() {
    var sec = Math.floor((Date.now() - startedAt) / 1000);
    $('time').textContent = pad(Math.floor(sec / 60)) + ':' + pad(sec % 60);
    var db = level > 0 ? 20 * Math.log10(level) : -120;
    var pos = Math.max(0, Math.min(1, (db + 60) / 60));
    $('bar').style.width = (pos * 100) + '%';
    $('bar').style.setProperty('--full', (100 / Math.max(pos, 0.01)) + '%');
    if (pos >= peakPos) { peakPos = pos; peakAt = Date.now(); }
    else if (Date.now() - peakAt > 1500) peakPos = Math.max(pos, peakPos - 0.02);
    $('peak').style.left = 'calc(' + (peakPos * 100) + '% - 3px)';
    $('meter').classList.toggle('hot', level > 0.97);
    level *= 0.8;
  }

  // ---------------------------------------------------------------- UI
  function renderList() {
    return allRecs().then(function (recs) {
      var ul = $('list');
      ul.textContent = '';
      var waiting = 0;
      if (recs.length === 0) {
        var li0 = document.createElement('li'); li0.className = 'small'; li0.textContent = 'まだありません'; ul.appendChild(li0);
      }
      recs.slice(0, 20).forEach(function (r) {
        var li = document.createElement('li');
        var a = document.createElement('span');
        a.textContent = r.name + '  ' + Math.round(r.seconds || 0) + ' 秒';
        var b = document.createElement('span');
        if (r.status === 'done') { b.className = 'st-done'; b.textContent = 'Drive に送信済み'; }
        else if (r.status === 'fail') { b.className = 'st-fail'; b.textContent = '失敗: ' + r.error; waiting++; }
        else { b.className = 'st-wait'; b.textContent = '送信待ち'; waiting++; }
        li.appendChild(a); li.appendChild(b); ul.appendChild(li);
      });
      $('retry').classList.toggle('hidden', waiting === 0);
    });
  }

  $('rec').addEventListener('click', function () {
    var p = recording ? stopRecording() : startRecording();
    p.catch(function (e) { $('info').textContent = 'エラー: ' + (e && e.message ? e.message : e); });
  });
  $('gear').addEventListener('click', function () { $('settings').classList.toggle('hidden'); });
  $('save').addEventListener('click', function () {
    saveSettings({ clientId: $('clientId').value.trim(), folder: folderIdOf($('folder').value) });
    $('info').textContent = '設定を保存しました';
  });
  $('connect').addEventListener('click', function () {
    saveSettings({ clientId: $('clientId').value.trim(), folder: folderIdOf($('folder').value) });
    requestToken().then(function () { return uploadPending(); }).catch(function (e) { $('auth').textContent = String(e.message || e); });
  });
  $('retry').addEventListener('click', function () {
    var go = tokenValid() ? Promise.resolve() : requestToken();
    go.then(function () {
      return allRecs().then(function (recs) {
        return Promise.all(recs.filter(function (r) { return r.status === 'fail'; }).map(function (r) { r.status = 'wait'; return putRec(r); }));
      });
    }).then(uploadPending).catch(function (e) { $('info').textContent = 'エラー: ' + (e.message || e); });
  });

  var s0 = loadSettings();
  $('clientId').value = s0.clientId || '';
  $('folder').value = s0.folder || '';
  if (!s0.clientId || !s0.folder) $('settings').classList.remove('hidden');
  else $('settings').classList.add('hidden');
  showAuth();
  renderList();
  $('bar').style.width = '0%';
  if ('serviceWorker' in navigator) navigator.serviceWorker.register('sw.js').catch(function () {});
})();
