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

  // ---------------------------------------------------------------- input: the level check and the recording share one open microphone
  var ctx = null, stream = null, node = null, source = null, chunks = [], channels = 1, rate = TARGET_RATE, bits = 24;
  var opening = null; // the microphone is being opened
  var recording = false, monitoring = false, flushing = false, flushDone = null, startedAt = 0, timer = null, wake = null;
  var meter = null;                    // loudness (Loudness.createMeter)
  var peakNow = [0, 0], peakHold = [0, 0], clipped = false, peakPos = 0, peakAt = 0;
  var formatNote = '';

  // the format the user chose (kept on this phone)
  var FORMAT_KEY = 'wtrec.format';
  function loadFormat() {
    var f = {};
    try { f = JSON.parse(localStorage.getItem(FORMAT_KEY) || '{}'); } catch (e) { /* ignore */ }
    return { channels: f.channels === 'mono' ? 'mono' : 'stereo', rate: f.rate === 44100 ? 44100 : 48000, bits: f.bits === 16 ? 16 : 24 };
  }
  function saveFormat(f) {
    try { localStorage.setItem(FORMAT_KEY, JSON.stringify(f)); } catch (e) { /* private mode */ }
  }
  var format = loadFormat();

  function pad(n) { return (n < 10 ? '0' : '') + n; }
  function stamp(d) {
    return d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate()) + '_' + pad(d.getHours()) + '-' + pad(d.getMinutes()) + '-' + pad(d.getSeconds());
  }
  function formatText(ch, hz, b) { return hz + ' Hz / ' + b + ' bit / ' + (ch === 2 ? 'ステレオ' : 'モノラル'); }
  function dbOf(v) { return v > 0 ? 20 * Math.log10(v) : -Infinity; }
  function fmtDb(v) { return v === -Infinity || v !== v ? '--' : v.toFixed(1); }

  function wakeOn() {
    if (navigator.wakeLock && !wake) navigator.wakeLock.request('screen').then(function (l) { wake = l; }, function () {});
  }
  function wakeOff() {
    if (wake) { wake.release(); wake = null; }
  }

  function resetReadout() {
    peakNow = [0, 0]; peakHold = [0, 0]; clipped = false;
    if (meter) meter.reset();
  }

  // One block of input from the worklet: it feeds the meters, and it is kept only while recording.
  function onBlock(e) {
    var d = e.data;
    if (d === 'flushed') { if (flushDone) flushDone(); return; }
    if (!Array.isArray(d)) return;
    if (recording || flushing) chunks.push(d);
    if (!meter) return;

    var pk = WavEnc.peaksOf(d);
    for (var c = 0; c < pk.length && c < 2; c++) {
      if (pk[c] > peakNow[c]) peakNow[c] = pk[c];
      if (pk[c] > peakHold[c]) peakHold[c] = pk[c];
      if (pk[c] >= 0.999) clipped = true;
    }
    meter.push(d);
  }

  // Opens the microphone (once; the level check and the recording both use it).
  function openInput() {
    if (stream) return Promise.resolve();
    if (opening) return opening;
    opening = openInputNow().then(function () { opening = null; }, function (e) { opening = null; throw e; });
    return opening;
  }

  function openInputNow() {
    var wantStereo = format.channels === 'stereo';
    var constraints = { audio: { channelCount: { ideal: wantStereo ? 2 : 1 }, sampleRate: { ideal: format.rate }, echoCancellation: false, noiseSuppression: false, autoGainControl: false } };
    return navigator.mediaDevices.getUserMedia(constraints).then(function (s) {
      stream = s;
      ctx = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: format.rate });
      rate = ctx.sampleRate;
      return ctx.audioWorklet.addModule('worklet.js');
    }).then(function () {
      source = ctx.createMediaStreamSource(stream);
      var have = (stream.getAudioTracks()[0].getSettings().channelCount) || 1;
      // stereo only if the microphone really gives two channels; mono is always one (a two-channel input is mixed down by the browser)
      channels = wantStereo ? Math.min(2, Math.max(1, have)) : 1;
      bits = format.bits;
      formatNote = (wantStereo && channels === 1 ? ' (この端末のマイクは 1 チャンネルなので、モノラルになります)' : '')
                   + (rate !== format.rate ? ' (端末が ' + format.rate + ' Hz に対応していません)' : '');

      meter = Loudness.createMeter(rate, channels);
      resetReadout();
      chunks = [];
      node = new AudioWorkletNode(ctx, 'recorder', { channelCount: channels, channelCountMode: 'explicit', numberOfOutputs: 0 });
      node.port.onmessage = onBlock;
      source.connect(node);
      wakeOn();
      tick();
      timer = setInterval(tick, 100);
    }).catch(function (e) {
      closeInput();
      throw e;
    });
  }

  function closeInput() {
    clearInterval(timer);
    timer = null;
    if (node) node.port.onmessage = null;
    try { if (source) source.disconnect(); } catch (e) { /* already gone */ }
    if (stream) stream.getTracks().forEach(function (t) { t.stop(); });
    if (ctx) ctx.close();
    stream = ctx = node = source = meter = null;
    monitoring = false;
    wakeOff();
    peakNow = [0, 0];
    showIdle();
  }

  function showIdle() {
    $('bar').style.width = '0%';
    $('peak').style.left = '0%';
    $('meter').classList.remove('hot');
    $('time').textContent = '00:00';
    ['lm', 'ls', 'li'].forEach(function (id) { $('lufs').querySelector('[data-k="' + id + '"]').textContent = '--'; });
    $('peaks').textContent = 'ピーク  L --  /  R --  dBFS';
    $('clip').classList.add('hidden');
    $('monitor').textContent = '入力レベルを確認';
    $('monitor').classList.remove('on');
    peakPos = 0;
  }

  function setBusy() {
    var busy = recording;
    ['segCh', 'segRate', 'segBits'].forEach(function (id) { $(id).classList.toggle('disabled', busy); });
    $('monitor').disabled = busy;
  }

  function startMonitor() {
    return openInput().then(function () {
      monitoring = true;
      $('monitor').textContent = '確認を止める';
      $('monitor').classList.add('on');
      $('info').textContent = 'レベル確認中 (録音はしていません)  ' + formatText(channels, rate, bits) + formatNote;
    });
  }

  function stopMonitor() {
    closeInput();
    $('info').textContent = '待機中';
  }

  function startRecording() {
    return openInput().then(function () {
      meter.reset();                    // the integrated loudness is that of this recording
      peakHold = [0, 0]; clipped = false;
      chunks = [];
      recording = true;
      monitoring = false;
      startedAt = Date.now();
      setBusy();
      $('rec').textContent = '停止';
      $('rec').classList.add('on');
      $('monitor').textContent = '入力レベルを確認';
      $('monitor').classList.remove('on');
      $('info').textContent = formatText(channels, rate, bits) + formatNote;
    });
  }

  function stopRecording() {
    recording = false;
    $('rec').classList.remove('on');
    $('rec').textContent = '録音';
    $('info').textContent = '保存中...';

    // the pop-up has to open from this tap, so ask for the token right now if there is none
    var auth = tokenValid() ? Promise.resolve() : requestToken().catch(function () { /* the file waits for a later tap */ });

    return new Promise(function (resolve) {
      var done = false;
      var finish = function () { if (!done) { done = true; flushing = false; flushDone = null; resolve(); } };
      flushing = true;
      flushDone = finish;
      node.port.postMessage('flush');
      setTimeout(finish, 500);
    }).then(function () {
      var fmt = { channels: channels, rate: rate, bits: bits };
      var enc = WavEnc.encodeWav(chunks, fmt.channels, fmt.rate, fmt.bits);
      chunks = [];
      closeInput();
      setBusy();

      var blob = new Blob(enc.parts, { type: 'audio/wav' });
      var name = stamp(new Date(startedAt)) + '.wav';
      return putRec({ id: name, name: name, blob: blob, size: blob.size, status: 'wait', error: '', createdAt: startedAt, seconds: enc.frames / fmt.rate, format: fmt })
        .then(function () { $('info').textContent = '保存しました: ' + name; return renderList(); });
    }).then(function () { return auth; }).then(function () { return uploadPending(); });
  }

  // 10 times a second: the time, the level meter (peak, with a hold) and the loudness figures
  function tick() {
    var sec = recording ? Math.floor((Date.now() - startedAt) / 1000) : 0;
    $('time').textContent = pad(Math.floor(sec / 60)) + ':' + pad(sec % 60);

    var level = Math.max(peakNow[0], peakNow[1]);
    var db = level > 0 ? 20 * Math.log10(level) : -120;
    var pos = Math.max(0, Math.min(1, (db + 60) / 60));
    $('bar').style.width = (pos * 100) + '%';
    $('bar').style.setProperty('--full', (100 / Math.max(pos, 0.01)) + '%');
    if (pos >= peakPos) { peakPos = pos; peakAt = Date.now(); }
    else if (Date.now() - peakAt > 1500) peakPos = Math.max(pos, peakPos - 0.02);
    $('peak').style.left = 'calc(' + (peakPos * 100) + '% - 3px)';
    $('meter').classList.toggle('hot', level > 0.97);
    peakNow = [peakNow[0] * 0.8, peakNow[1] * 0.8];

    if (meter) {
      var set = function (k, v) { $('lufs').querySelector('[data-k="' + k + '"]').textContent = fmtDb(v); };
      set('lm', meter.momentary());
      set('ls', meter.shortTerm());
      set('li', meter.integrated());
    }
    $('peaks').textContent = 'ピーク  L ' + fmtDb(dbOf(peakHold[0])) + (channels === 2 ? '  /  R ' + fmtDb(dbOf(peakHold[1])) : '') + '  dBFS';
    $('clip').classList.toggle('hidden', !clipped);
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
        a.textContent = r.name + '  ' + Math.round(r.seconds || 0) + ' 秒' + (r.format ? '  ' + (r.format.channels === 2 ? 'ステレオ' : 'モノラル') + ' ' + r.format.bits + 'bit' : '');
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
  $('monitor').addEventListener('click', function () {
    if (recording) return;
    var p = monitoring ? Promise.resolve(stopMonitor()) : startMonitor();
    p.catch(function (e) { $('info').textContent = 'エラー: ' + (e && e.message ? e.message : e); });
  });
  // tap the meter: peak hold, the clip mark and the integrated loudness start over
  $('meterbox').addEventListener('click', function () { resetReadout(); tick(); });

  // format: stereo / mono, 48 / 44.1 kHz, 24 / 16 bit (not while recording; a running level check is restarted with the new format)
  function showFormat() {
    [['segCh', 'channels'], ['segRate', 'rate'], ['segBits', 'bits']].forEach(function (pair) {
      var buttons = $(pair[0]).querySelectorAll('button');
      for (var i = 0; i < buttons.length; i++) buttons[i].classList.toggle('on', String(format[pair[1]]) === buttons[i].getAttribute('data-v'));
    });
  }
  [['segCh', 'channels'], ['segRate', 'rate'], ['segBits', 'bits']].forEach(function (pair) {
    $(pair[0]).addEventListener('click', function (ev) {
      var b = ev.target.closest('button');
      if (!b || recording) return;
      var v = b.getAttribute('data-v');
      format[pair[1]] = pair[1] === 'channels' ? v : Number(v);
      saveFormat(format);
      showFormat();

      // the bit depth only matters when the file is written; the channels and the rate need the microphone opened again
      if (pair[1] === 'bits') { if (stream) bits = format.bits; if (monitoring) $('info').textContent = 'レベル確認中 (録音はしていません)  ' + formatText(channels, rate, bits) + formatNote; return; }
      if (monitoring || opening) {
        (opening || Promise.resolve()).catch(function () { /* reported where it was started */ }).then(function () {
          if (recording) return;
          closeInput();
          return startMonitor();
        }).catch(function (e) { $('info').textContent = 'エラー: ' + (e && e.message ? e.message : e); });
      }
    });
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

  // a setup link (#cid=<client id>&f=<folder URL or ID>) fills the settings, so nothing has to be typed on the phone
  (function () {
    var h = location.hash.replace(/^#/, '');
    if (!h) return;
    var p = {};
    h.split('&').forEach(function (kv) { var i = kv.indexOf('='); if (i > 0) p[kv.slice(0, i)] = decodeURIComponent(kv.slice(i + 1)); });
    if (p.cid || p.f) {
      var cur = loadSettings();
      saveSettings({ clientId: (p.cid || cur.clientId || '').trim(), folder: p.f ? folderIdOf(p.f) : cur.folder });
      history.replaceState(null, '', location.pathname + location.search);
    }
  })();

  var s0 = loadSettings();
  $('clientId').value = s0.clientId || '';
  $('folder').value = s0.folder || '';
  if (!s0.clientId || !s0.folder) $('settings').classList.remove('hidden');
  else $('settings').classList.add('hidden');
  showAuth();
  renderList();
  showFormat();
  showIdle();
  if ('serviceWorker' in navigator) navigator.serviceWorker.register('sw.js').catch(function () {});
})();
