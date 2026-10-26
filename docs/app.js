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

  var waitingCount = 0; // recordings that are not in Drive yet

  var nat = { authOk: false, folderSet: false, uploadError: '' }; // what the Android app says about Google and sending
  function isNative() { return !!(window.WTNative && WTNative.available); }

  function showAuth() {
    if (isNative()) $('auth').textContent = (nat.authOk ? '接続済み' : '未接続') + (nat.uploadError ? '  ·  ' + nat.uploadError : '');
    else $('auth').textContent = tokenValid() ? '接続済み (約 1 時間有効)' : '未接続';
    showConnect();
  }

  // Recordings are never held up by a sign-in: they wait, and this bar offers to connect when it suits the user.
  function showConnect() {
    if (isNative()) {
      var needNative = waitingCount > 0 && nat.folderSet && !nat.authOk;
      $('connectBar').classList.toggle('hidden', !needNative);
      if (needNative) $('connectText').textContent = '送信待ちが ' + waitingCount + ' 件あります。Google に接続すると送信します';
      return;
    }
    var s = loadSettings();
    var need = waitingCount > 0 && !tokenValid() && !!s.clientId && !!folderIdOf(s.folder);
    $('connectBar').classList.toggle('hidden', !need);
    if (need) $('connectText').textContent = '送信待ちが ' + waitingCount + ' 件あります。Google に接続すると送信します (接続は約 1 時間有効)';
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
  var peakNow = [0, 0], peakHold = [0, 0], clipped = false, peakPos = [0, 0], peakAt = [0, 0];
  var formatNote = '';
  var recPeak = 0, badSamples = 0; // the loudest sample of this recording; numbers that were not numbers

  // stereo scope (Polar Sample / Lissajous): drawn every frame while the microphone is open
  var SCOPE_KEY = 'wtrec.scope';
  var scope = Scope.create($('scope')), scopeRaf = 0;
  try { scope.setMode(localStorage.getItem(SCOPE_KEY)); } catch (e) { /* ignore */ }
  function scopeLoop() { scope.draw(); scopeRaf = requestAnimationFrame(scopeLoop); }
  function scopeStart() { if (!scopeRaf) scopeRaf = requestAnimationFrame(scopeLoop); }
  function scopeStop() { if (scopeRaf) cancelAnimationFrame(scopeRaf); scopeRaf = 0; }

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
    badSamples += WavEnc.sanitize(d);
    if (calSink) calSink(d);
    if ((recording || flushing) && !nativeOpen) chunks.push(d); // the app writes its own file: nothing piles up in the page
    if (!meter) return;
    if (channels === 2 && d.length >= 2) scope.push(d, rate);

    var pk = WavEnc.peaksOf(d);
    if (recording || flushing) recPeak = Math.max(recPeak, pk[0] || 0, pk[1] || 0);
    if (channels === 2 && d.length >= 2 && !diag.checked && Math.max(pk[0], pk[1]) > 0.001) {
      // left and right of a real stereo microphone are never sample for sample the same
      var l = d[0], r = d[1];
      for (var k = 0; k < l.length; k++) { var df = l[k] - r[k]; if (df < 0) df = -df; if (df > diag.maxDiff) diag.maxDiff = df; }
      diag.loud += l.length / rate;
    }
    for (var c = 0; c < pk.length && c < 2; c++) {
      if (pk[c] > peakNow[c]) peakNow[c] = pk[c];
      if (pk[c] > peakHold[c]) peakHold[c] = pk[c];
      if (pk[c] >= 0.999) clipped = true;
    }
    meter.push(d);
  }

  // Opens the microphone (once; the level check and the recording both use it).
  var nativeOpen = false; // the Android app holds the microphone (window.WTNative, native.js)

  function openInput() {
    if (stream || nativeOpen) return Promise.resolve();
    if (opening) return opening;
    opening = openInputNow().then(function () { opening = null; }, function (e) { opening = null; throw e; });
    return opening;
  }

  // What the browser says about the microphone, what really arrived, and (for two channels) whether left and right are the same sound.
  var diag = { label: '', reported: null, capMax: null, got: 0, how: '', maxDiff: 0, loud: 0, checked: false, dual: false };

  function showDiag() {
    var t = '入力: ' + (diag.label || '(名前なし)') + '  ·  ブラウザの報告: ' + (diag.reported === null ? 'なし' : diag.reported + ' ch')
          + (diag.capMax !== null ? ' (最大 ' + diag.capMax + ')' : '') + '  ·  届いた音: ' + diag.got + ' ch'
          + (diag.how ? '  ·  要求: ' + diag.how : '')
          + (diag.dual ? '  ·  左右がまったく同じ音です (ステレオではありません)' : '')
          + (badSamples ? '  ·  数値でない値を ' + badSamples + ' 個受け取りました' : '');
    $('diag').textContent = t;
  }

  function infoNow() {
    if (recording) $('info').textContent = formatText(channels, rate, bits) + formatNote;
    else if (monitoring) $('info').textContent = 'レベル確認中 (録音はしていません)  ' + formatText(channels, rate, bits) + formatNote;
  }

  // Stereo is asked for exactly first; a browser that cannot do that (OverconstrainedError) gets the softer request.
  function getStream(wantStereo) {
    var ask = function (count) {
      return navigator.mediaDevices.getUserMedia({ audio: { channelCount: count, sampleRate: { ideal: format.rate }, echoCancellation: false, noiseSuppression: false, autoGainControl: false } });
    };
    if (!wantStereo) return ask({ ideal: 1 }).then(function (s) { diag.how = '1 ch'; return s; });
    return ask({ exact: 2 }).then(function (s) { diag.how = '2 ch'; return s; }, function (e) {
      if (e && (e.name === 'OverconstrainedError' || e.name === 'ConstraintNotSatisfiedError'))
        return ask({ ideal: 2 }).then(function (s) { diag.how = '2 ch (できれば)'; return s; });
      throw e;
    });
  }

  function openInputNow() {
    var wantStereo = format.channels === 'stereo';
    diag = { label: '', reported: null, capMax: null, got: 0, how: '', maxDiff: 0, loud: 0, checked: false, dual: false };

    if (window.WTNative && WTNative.available) { // the Android app reads the microphone; blocks come through native.js
      return WTNative.open({ stereo: wantStereo, rate: format.rate }).then(function (info) {
        rate = info.rate;
        diag.label = info.label + (info.effects ? ' [' + info.effects + ']' : '');
        diag.reported = info.channels;
        diag.how = wantStereo ? '2 ch' : '1 ch';
        nativeOpen = true;
        WTNative.onBlock = onBlock;
        WTNative.onClosed = function () { if (!recording) closeInput(); };
        return finishOpen(info.channels, wantStereo);
      }).catch(function (e) { closeInput(); throw e; });
    }

    return getStream(wantStereo).then(function (s) {
      stream = s;
      var track = s.getAudioTracks()[0];
      diag.label = track.label || '';
      var st = track.getSettings ? track.getSettings() : {};
      diag.reported = st.channelCount === undefined ? null : st.channelCount;
      var caps = track.getCapabilities ? track.getCapabilities() : null;
      diag.capMax = caps && caps.channelCount ? caps.channelCount.max : null;
      ctx = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: format.rate });
      rate = ctx.sampleRate;
      return ctx.audioWorklet.addModule('worklet.js?v=5');
    }).then(function () {
      source = ctx.createMediaStreamSource(stream);
      // 'max': the node takes as many channels as the input really has, so the first block of sound says how many there are
      // (the browser's own report is not always there, and a missing value must not be taken for "one channel")
      node = new AudioWorkletNode(ctx, 'recorder', { channelCount: 2, channelCountMode: 'max', numberOfOutputs: 0 });
      source.connect(node);
      return new Promise(function (resolve) {
        var done = false;
        var finish = function (n) { if (!done) { done = true; node.port.onmessage = null; resolve(n); } };
        node.port.onmessage = function (e) { if (Array.isArray(e.data)) finish(e.data.length); };
        setTimeout(function () { finish(diag.reported || 1); }, 1500);
      });
    }).then(function (got) {
      node.channelCountMode = 'explicit';
      node.channelCount = wantStereo ? (got >= 2 ? 2 : 1) : 1;
      return finishOpen(got, wantStereo);
    }).catch(function (e) {
      closeInput();
      throw e;
    });
  }

  // The input is open and its channel count is known: set up the meters and start listening.
  function finishOpen(got, wantStereo) {
    return Promise.resolve().then(function () {
      diag.got = got;
      // stereo only if two channels really arrive; mono is always one (a wider input is mixed down by the browser, never the other way round)
      channels = wantStereo ? (got >= 2 ? 2 : 1) : 1;
      bits = format.bits;
      formatNote = (wantStereo && channels === 1 ? ' (ブラウザがこのマイクを 1 チャンネルでしか渡さないため、モノラルです)' : '')
                   + (rate !== format.rate ? ' (端末が ' + format.rate + ' Hz に対応していません)' : '');

      meter = Loudness.createMeter(rate, channels);
      showChannels(channels);
      resetReadout();
      chunks = [];
      if (node) node.port.onmessage = onBlock;
      showDiag();
      wakeOn();
      scope.clear();
      scopeStart();
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
    scopeStop();
    if (nativeOpen) { nativeOpen = false; WTNative.onBlock = null; WTNative.close().catch(function () {}); }
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
    showChannels(format.channels === 'stereo' ? 2 : 1);
    ['L', 'R'].forEach(function (c) {
      var m = $('meter' + c);
      m.style.setProperty('--lvl', '0');
      m.style.setProperty('--pk', '0');
      m.classList.remove('hot');
    });
    $('time').textContent = '00:00';
    ['lm', 'ls', 'li'].forEach(function (id) { $('lufs').querySelector('[data-k="' + id + '"]').textContent = '--'; });
    $('peaks').textContent = 'ピーク  L --  /  R --  dBFS';
    $('clip').classList.add('hidden');
    $('monitor').textContent = '入力レベルを確認';
    $('monitor').classList.remove('on');
    peakPos = [0, 0];
    scope.clear();
    showCorr(null);
  }

  // stereo: two bars (L and R), the scope and the correlation; mono: one bar
  function showChannels(n) {
    $('rowR').classList.toggle('hidden', n !== 2);
    $('labL').textContent = n === 2 ? 'L' : 'M';
    $('scopebox').classList.toggle('hidden', n !== 2);
    $('corr').classList.toggle('hidden', n !== 2);
    scope.relayout();
  }

  function showCorr(v) {
    $('corrNum').textContent = v === null ? '--' : (v >= 0 ? '+' : '') + v.toFixed(2);
    $('corrMark').style.setProperty('--c', v === null ? 0.5 : (v + 1) / 2);
    $('corrMark').classList.toggle('neg', v !== null && v < -0.2);
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
      return nativeOpen ? WTNative.startRecording({ bits: format.bits }) : null; // the app starts writing the file (and fixes left/right)
    }).then(function () {
      meter.reset();                    // the integrated loudness is that of this recording
      peakHold = [0, 0]; clipped = false;
      recPeak = 0; badSamples = 0;
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

  // Android app: the file was written by the app; this only shows what happened.
  function stopNative() {
    return WTNative.stopRecording().then(function (r) {
      closeInput();
      setBusy();
      if (r.silent) {
        $('info').textContent = '録音が無音でした (マイクの音が届いていません)。保存しません。もう一度「入力レベルを確認」でメーターが動くか見てください';
        return;
      }
      $('info').textContent = '保存しました: ' + r.files.join(', ') + (r.files.length > 1 ? ' (長いので分割しました)' : '') + (nat.authOk ? '  (Drive へ送信します)' : '  (Google に未接続: 送信待ちです)')
        + (r.error ? '  ※書き込み中のエラー: ' + r.error : '');
      return renderList();
    }, function (e) {
      closeInput();
      setBusy();
      $('info').textContent = '録音を終えられませんでした: ' + (e && e.message || e);
    });
  }

  function stopRecording() {
    recording = false;
    $('rec').classList.remove('on');
    $('rec').textContent = '録音';
    $('info').textContent = '保存中...';
    if (nativeOpen) return stopNative();

    return new Promise(function (resolve) {
      var done = false;
      var finish = function () { if (!done) { done = true; flushing = false; flushDone = null; resolve(); } };
      flushing = true;
      flushDone = finish;
      if (nativeOpen) setTimeout(finish, 100); // blocks already sent are in the page's message queue; there is nothing to flush
      else { node.port.postMessage('flush'); setTimeout(finish, 500); }
    }).then(function () {
      // a recording in which nothing but zeros arrived is of no use: say so instead of saving and sending an empty file
      if (!(recPeak > 1e-6)) {
        chunks = [];
        showDiag();
        closeInput();
        setBusy();
        $('info').textContent = '録音が無音でした (マイクの音が届いていません)。保存しません。もう一度「入力レベルを確認」でメーターが動くか見てください';
        return;
      }

      var fmt = { channels: channels, rate: rate, bits: bits };
      var enc = WavEnc.encodeWav(chunks, fmt.channels, fmt.rate, fmt.bits);
      chunks = [];
      closeInput();
      setBusy();

      var blob = new Blob(enc.parts, { type: 'audio/wav' });
      var name = stamp(new Date(startedAt)) + '.wav';
      return putRec({ id: name, name: name, blob: blob, size: blob.size, status: 'wait', error: '', createdAt: startedAt, seconds: enc.frames / fmt.rate, format: fmt })
        .then(function () { $('info').textContent = '保存しました: ' + name + (tokenValid() ? '' : '  (Google に未接続: 送信待ちです)'); return renderList(); })
        .then(function () { return tokenValid() ? uploadPending() : undefined; }); // no sign-in window here: that waits for the user
    });
  }

  // 10 times a second: the time, the level meter (peak, with a hold) and the loudness figures
  function tick() {
    var sec = recording ? Math.floor((Date.now() - startedAt) / 1000) : 0;
    $('time').textContent = pad(Math.floor(sec / 60)) + ':' + pad(sec % 60);

    ['L', 'R'].forEach(function (c, ch) {
      var level = peakNow[ch];
      var db = level > 0 ? 20 * Math.log10(level) : -120;
      var pos = Math.max(0, Math.min(1, (db + 60) / 60));
      var m = $('meter' + c);
      if (pos >= peakPos[ch]) { peakPos[ch] = pos; peakAt[ch] = Date.now(); }
      else if (Date.now() - peakAt[ch] > 1500) peakPos[ch] = Math.max(pos, peakPos[ch] - 0.02);
      m.style.setProperty('--lvl', pos);
      m.style.setProperty('--full', (100 / Math.max(pos, 0.01)) + '%');
      m.style.setProperty('--pk', peakPos[ch]);
      m.classList.toggle('hot', level > 0.97);
    });
    peakNow = [peakNow[0] * 0.8, peakNow[1] * 0.8];

    if (channels === 2 && !diag.checked && diag.loud >= 1) {
      diag.checked = true;
      diag.dual = diag.maxDiff < 1e-6;
      if (diag.dual) formatNote += ' (左右がまったく同じ音です)';
      showDiag();
      infoNow();
    }
    if (meter) {
      var set = function (k, v) { $('lufs').querySelector('[data-k="' + k + '"]').textContent = fmtDb(v); };
      set('lm', meter.momentary());
      set('ls', meter.shortTerm());
      set('li', meter.integrated());
    }
    if (channels === 2 && stream) showCorr(scope.correlation());
    $('peaks').textContent = 'ピーク  L ' + fmtDb(dbOf(peakHold[0])) + (channels === 2 ? '  /  R ' + fmtDb(dbOf(peakHold[1])) : '') + '  dBFS';
    $('clip').classList.toggle('hidden', !clipped);
  }

  // ---------------------------------------------------------------- UI
  function nativeRecs() {
    return WTNative.listRecordings().then(function (list) {
      return list.map(function (r) {
        return { name: r.name, seconds: r.seconds, status: r.status === 'done' ? 'done' : 'wait',
                 format: r.channels ? { channels: r.channels, bits: r.bits } : null, recovered: r.recovered };
      });
    });
  }

  function renderList() {
    return (window.WTNative && WTNative.available ? nativeRecs() : allRecs()).then(function (recs) {
      var ul = $('list');
      ul.textContent = '';
      var waiting = 0;
      if (recs.length === 0) {
        var li0 = document.createElement('li'); li0.className = 'small'; li0.textContent = 'まだありません'; ul.appendChild(li0);
      }
      recs.slice(0, 20).forEach(function (r) {
        var li = document.createElement('li');
        var a = document.createElement('span');
        a.textContent = r.name + (r.recovered ? ' (回復)' : '') + '  ' + Math.round(r.seconds || 0) + ' 秒' + (r.format ? '  ' + (r.format.channels === 2 ? 'ステレオ' : 'モノラル') + ' ' + r.format.bits + 'bit' : '');
        var b = document.createElement('span');
        if (r.status === 'done') { b.className = 'st-done'; b.textContent = 'Drive に送信済み'; }
        else if (r.status === 'fail') { b.className = 'st-fail'; b.textContent = '失敗: ' + r.error; waiting++; }
        else { b.className = 'st-wait'; b.textContent = '送信待ち'; waiting++; }
        li.appendChild(a); li.appendChild(b); ul.appendChild(li);
      });
      $('retry').classList.toggle('hidden', waiting === 0);
      waitingCount = waiting;
      showConnect();
    });
  }

  // ---- screen orientation: auto -> landscape -> portrait. The phone turns the screen by itself when it is tilted; this holds it.
  var ORIENT_KEY = 'wtrec.orientation';
  var ORIENT_TEXT = { auto: '向き: 自動', landscape: '向き: 横', portrait: '向き: 縦' };
  var orient = 'landscape', orientChosen = false, enteredFullscreen = false; // landscape until the user chooses otherwise
  try {
    var savedOrient = localStorage.getItem(ORIENT_KEY);
    if (savedOrient === 'auto' || savedOrient === 'landscape' || savedOrient === 'portrait') { orient = savedOrient; orientChosen = true; }
  } catch (e) { /* ignore */ }

  function showOrient() {
    $('orient').textContent = ORIENT_TEXT[orient];
    $('orient').classList.toggle('on', orient !== 'auto');
  }

  // Chrome on Android only locks the orientation in full screen (or in some installed apps), so if the plain lock is refused the page goes
  // full screen first (that needs this tap). iPhones cannot lock at all.
  function lockOrientation(mode) {
    var so = window.screen && screen.orientation;
    if (mode === 'auto') {
      if (so && so.unlock) { try { so.unlock(); } catch (e) { /* nothing was locked */ } }
      if (enteredFullscreen && document.fullscreenElement && document.exitFullscreen) document.exitFullscreen().catch(function () {});
      enteredFullscreen = false;
      return Promise.resolve();
    }
    var cannot = new Error('この端末 (ブラウザ) では向きを固定できません。本体の「画面の自動回転」をオフにしてから、本体を傾けてください');
    if (!so || !so.lock) return Promise.reject(cannot);
    var want = mode === 'landscape' ? 'landscape' : 'portrait';
    return Promise.resolve().then(function () { return so.lock(want); }).catch(function () {
      var el = document.documentElement;
      if (!el.requestFullscreen || document.fullscreenElement) throw cannot;
      return el.requestFullscreen({ navigationUI: 'hide' }).then(function () { enteredFullscreen = true; return so.lock(want); });
    }).catch(function () { throw cannot; });
  }

  // quiet: the default is applied on its own; a phone that cannot lock the screen is not told about it every time
  function setOrientation(mode, quiet) {
    return lockOrientation(mode).then(function () {
      orient = mode;
      if (!quiet) orientChosen = true;
      if (!quiet) { try { localStorage.setItem(ORIENT_KEY, orient); } catch (e) { /* private mode */ } }
      showOrient();
    }, function (e) {
      orient = 'auto';
      showOrient();
      if (!quiet) $('info').textContent = e.message;
    });
  }

  $('orient').addEventListener('click', function () {
    setOrientation(orient === 'auto' ? 'landscape' : orient === 'landscape' ? 'portrait' : 'auto');
  });
  // leaving full screen (the back gesture) releases the lock
  document.addEventListener('fullscreenchange', function () {
    if (!document.fullscreenElement && enteredFullscreen) { enteredFullscreen = false; orient = 'auto'; showOrient(); }
  });
  // a saved choice is applied with the first tap (full screen cannot be entered without one)
  document.addEventListener('click', function (ev) {
    if (orient !== 'auto' && !(ev.target.closest && ev.target.closest('#orient'))) setOrientation(orient, !orientChosen);
  }, { once: true, capture: true });

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
  ['meterbox', 'peaksRow', 'lufs'].forEach(function (id) {
    $(id).addEventListener('click', function () { resetReadout(); tick(); });
  });
  // tap the scope: Polar Sample <-> Lissajous
  $('scopebox').addEventListener('click', function () {
    var m = scope.mode() === 'polar' ? 'lissajous' : 'polar';
    scope.setMode(m);
    try { localStorage.setItem(SCOPE_KEY, m); } catch (e) { /* private mode */ }
  });
  if (window.ResizeObserver) new ResizeObserver(function () { scope.relayout(); }).observe($('scopebox'));
  window.addEventListener('resize', function () { scope.relayout(); });

  // format: stereo / mono, 48 / 44.1 kHz, 24 / 16 bit (not while recording; a running level check is restarted with the new format)
  function showFormat() {
    [['segCh', 'channels'], ['segRate', 'rate'], ['segBits', 'bits']].forEach(function (pair) {
      var buttons = $(pair[0]).querySelectorAll('button');
      for (var i = 0; i < buttons.length; i++) buttons[i].classList.toggle('on', String(format[pair[1]]) === buttons[i].getAttribute('data-v'));
    });
    if (!stream && !opening) showChannels(format.channels === 'stereo' ? 2 : 1);
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
  // tabs: the recording screen holds only what is needed while recording; the format, the Drive settings and the history have their own tabs
  function showTab(name) {
    ['Rec', 'Hist', 'Set'].forEach(function (t) {
      $('tab' + t).classList.toggle('on', t === name);
      $('tabs').querySelector('[data-tab="' + t + '"]').classList.toggle('on', t === name);
    });
    if (name === 'Rec') scope.relayout();
  }
  $('tabs').addEventListener('click', function (ev) {
    var b = ev.target.closest('button');
    if (b) showTab(b.getAttribute('data-tab'));
  });
  $('connectNow').addEventListener('click', function () { $('retry').click(); });
  setInterval(showAuth, 30000); // the token runs out after about an hour: the bar appears by itself
  $('save').addEventListener('click', function () {
    if (isNative()) { WTNative.setSettings({ folder: folderIdOf($('folder').value) }).then(refreshNative); $('info').textContent = '設定を保存しました'; return; }
    saveSettings({ clientId: $('clientId').value.trim(), folder: folderIdOf($('folder').value) });
    $('info').textContent = '設定を保存しました';
  });
  $('connect').addEventListener('click', function () {
    if (isNative()) { connectNative(); return; }
    saveSettings({ clientId: $('clientId').value.trim(), folder: folderIdOf($('folder').value) });
    requestToken().then(function () { return uploadPending(); }).catch(function (e) { $('auth').textContent = String(e.message || e); });
  });
  $('retry').addEventListener('click', function () {
    if (isNative()) { if (nat.authOk) WTNative.upload().then(refreshNative); else connectNative(); return; }
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
  if (!isNative() && (!s0.clientId || !s0.folder)) showTab('Set'); // nothing to send to yet: start at the settings
  showAuth();
  renderList();
  showFormat();
  showOrient();
  showIdle();
  // ---- Android app: automatic left/right adjustment. The user taps the left edge of the screen, then the right edge; the app compares
  // the two channels in each part. If the left taps are louder on R (and the right taps on L), the channels are reversed for this rotation.
  var calSink = null;

  function calibrate() {
    var msg = $('calMsg'), btn = $('calBtn');
    if (recording || monitoring) { msg.textContent = '録音中・確認中は調整できません。止めてからもう一度どうぞ'; return; }
    btn.disabled = true;
    var energy = { left: [0, 0, 0], right: [0, 0, 0] }, phase = null;
    calSink = function (d) {
      if (!phase || d.length < 2) return;
      var l = d[0], r = d[1], el = 0, er = 0;
      for (var i = 0; i < l.length; i++) { el += l[i] * l[i]; er += r[i] * r[i]; }
      var e = energy[phase]; e[0] += el; e[1] += er; e[2] += l.length;
    };
    var step = function (text, name, seconds) {
      return new Promise(function (resolve) {
        var left = seconds;
        phase = name;
        var show = function () { msg.textContent = text + '  (あと ' + left + ' 秒)'; };
        show();
        var t = setInterval(function () { left--; if (left <= 0) { clearInterval(t); phase = null; resolve(); } else show(); }, 1000);
      });
    };
    var db = function (x) { return 10 * Math.log10(x + 1e-20); };
    msg.textContent = 'マイクを開いています...';
    openInput().then(function () {
      if (channels !== 2) throw new Error('この端末のマイクは 1 チャンネルなので、左右の調整はできません');
      msg.textContent = 'スマホを横に持って、そのままお待ちください...';
      return new Promise(function (r) { setTimeout(r, 2000); });
    }).then(function () {
      return step('画面の【左の端】を指で 5 回、トントンと叩いてください', 'left', 7);
    }).then(function () {
      return step('次に、画面の【右の端】を 5 回、トントンと叩いてください', 'right', 7);
    }).then(function () {
      var a = energy.left, b = energy.right;
      // how much more the left channel hears the left taps than the right taps; a correct setup gives a clearly positive number
      var score = (db(a[0]) - db(a[1])) - (db(b[0]) - db(b[1]));
      calSink = null;
      closeInput();
      if (Math.abs(score) < 4) { msg.textContent = 'はっきり判定できませんでした (差 ' + score.toFixed(1) + ' dB)。もう少し強く、画面の端を叩いてもう一度どうぞ'; return; }
      if (score > 0) { msg.textContent = '左右は合っています (差 ' + score.toFixed(1) + ' dB)。この向きの調整は不要でした'; return; }
      return WTNative.flipSwap().then(function () {
        msg.textContent = '左右が逆でした (差 ' + score.toFixed(1) + ' dB)。この向きは入れ替えるように直しました。確認したい時は、もう一度調整を行ってください';
      });
    }).catch(function (e) {
      calSink = null;
      closeInput();
      msg.textContent = '調整できませんでした: ' + (e && e.message || e);
    }).then(function () { btn.disabled = false; });
  }

  // ---- Android app: Google and sending are the app's business (Google's consent screen, a background sender). The page shows the state.
  function refreshNative() {
    return Promise.all([WTNative.getState(), WTNative.getSettings()]).then(function (r) {
      nat.authOk = !!r[0].authOk; nat.folderSet = !!r[0].folderSet; nat.uploadError = r[0].uploadError || '';
      if (document.activeElement !== $('folder')) $('folder').value = r[1].folder || '';
      showAuth();
      return renderList();
    }).catch(function () { /* the app is busy: the next round will do */ });
  }

  function connectNative() {
    var folder = folderIdOf($('folder').value);
    $('auth').textContent = 'Google に接続しています...';
    WTNative.setSettings({ folder: folder }).then(function () { return WTNative.connect(); }).then(refreshNative, function (e) {
      $('auth').textContent = String(e && e.message || e);
    });
  }

  if (isNative()) {
    $('calBox').classList.remove('hidden');
    $('calBtn').addEventListener('click', calibrate);
    $('clientId').classList.add('hidden');             // the app is identified by its package and signing key, not by a client ID typed here
    var cl = document.querySelector('label[for="clientId"]'); if (cl) cl.classList.add('hidden');
    refreshNative().then(function () { if (!nat.folderSet) showTab('Set'); });
    setInterval(refreshNative, 10000);                 // sending goes on in the background: the history follows
  }

  if (window.WTNative && WTNative.available) { var oe = $('orient'); if (oe) oe.classList.add('hidden'); } // the app is locked to landscape itself
  if (!(window.WTNative && WTNative.available) && 'serviceWorker' in navigator) navigator.serviceWorker.register('sw.js').catch(function () {});
})();
