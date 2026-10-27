// The link to the Android app (android/WaveTaggerRec). In a plain browser nothing here is active: WTNative.available is false.
// The app puts an object named WTNativeBridge into the page; the microphone is then read by the app, not by the browser.
(function () {
  'use strict';

  var bridge = window.WTNativeBridge;
  var WTNative = { available: !!bridge };
  window.WTNative = WTNative;
  if (!bridge) return;

  var nextId = 1;
  var waiting = {};
  var channels = 2;
  var transport = 'arraybuffer';

  // The app answers a call by running this.
  window.__wtResolve = function (id, ok, json) {
    var w = waiting[id];
    if (!w) return;
    delete waiting[id];
    if (ok) { try { w.resolve(json ? JSON.parse(json) : {}); } catch (e) { w.resolve({}); } }
    else w.reject(new Error(json || 'native error'));
  };

  function call(method, args) {
    return new Promise(function (resolve, reject) {
      var id = nextId++;
      waiting[id] = { resolve: resolve, reject: reject };
      try { bridge.call(id, method, JSON.stringify(args || {})); }
      catch (e) { delete waiting[id]; reject(e); }
    });
  }

  // A block is planar float32: all of channel 0, then all of channel 1. It reaches onBlock as the worklet's arrays would.
  function deliver(buffer) {
    if (!WTNative.onBlock) return;
    var all = new Float32Array(buffer);
    var frames = Math.floor(all.length / channels);
    var out = [];
    for (var c = 0; c < channels; c++) out.push(all.subarray(c * frames, (c + 1) * frames));
    WTNative.onBlock({ data: out });
  }

  window.addEventListener('message', function (e) {
    if (e.data instanceof ArrayBuffer) deliver(e.data);
  });

  // the fallback when the WebView cannot pass a binary message
  window.__wtBlock = function (b64) {
    var bin = atob(b64);
    var bytes = new Uint8Array(bin.length);
    for (var i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    deliver(bytes.buffer);
  };

  // the app closed the microphone by itself (the screen went to the background)
  window.__wtClosed = function () { if (WTNative.onClosed) WTNative.onClosed(); };

  /** Opens the microphone: {stereo, rate, source?}. Resolves with {channels, rate, label, effects, transport, rotation, swapped}. */
  WTNative.open = function (opts) {
    return call('open', opts).then(function (info) {
      channels = info.channels;
      transport = info.transport;
      return info;
    });
  };
  WTNative.close = function () { return call('close'); };
  /** The app writes the WAV itself (also with the screen off). startRecording({bits}) -> {name}; stopRecording() -> {files, seconds, peak, silent, error}. */
  WTNative.startRecording = function (opts) { return call('startRecording', opts); };
  WTNative.stopRecording = function () { return call('stopRecording'); };
  WTNative.listRecordings = function () { return call('listRecordings'); };
  WTNative.deleteRecording = function (name) { return call('deleteRecording', { name: name }); };
  WTNative.getState = function () { return call('getState'); };
  /** The automatic left/right adjustment found the channels reversed for the current rotation: swap them from now on. */
  WTNative.flipSwap = function () { return call('flipSwap'); };
  /** Google's consent screen (only when Google asks for it), then the waiting recordings are sent. */
  WTNative.connect = function () { return call('connect'); };
  WTNative.upload = function () { return call('upload'); };
  /** Android's share sheet for one recording (Drive, mail, chat...). */
  WTNative.share = function (name) { return call('share', { name: name }); };
  WTNative.getSettings = function () { return call('getSettings'); };
  WTNative.setSettings = function (s) { return call('setSettings', s); };
  WTNative.onBlock = null;
  WTNative.onClosed = null;
})();
