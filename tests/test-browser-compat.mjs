#!/usr/bin/env node
// Deterministic browser-policy regressions using the production client.
// This is a DOM/API double, not Safari, Windows, native input or media acceptance.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import { readFile } from 'node:fs/promises';
const source = await readFile(new URL('../app/browser/app.js', import.meta.url), 'utf8');
const checks = [];
const check = (name, value) => { assert.ok(value, name); checks.push(name); };
const tick = () => new Promise(resolve => setImmediate(resolve));
async function fixture({ audio = true, play = async () => {}, codecs } = {}) {
  class Element extends EventTarget {
    constructor() { super(); this.dataset = {}; this.hidden = false; this.value = ''; this.checked = false;
      this.parentElement = { hidden: false }; this.paused = true; this.muted = false; this.srcObject = null; }
    setAttribute() {}
    before() {}
    after() {}
    focus() {}
    getBoundingClientRect() { return { width: 960, height: 540, left: 0, top: 0 }; }
    replaceChildren() {}
    querySelector() { return new Element(); }
    pause() { this.paused = true; }
    async play() {
      if (!this.srcObject) return;
      await play(this);
      if (this.srcObject) { this.paused = false; this.videoWidth = 960; this.videoHeight = 540; this.dispatchEvent(new Event('playing')); }
    }
  }
  const elements = new Map();
  const get = id => { if (!elements.has(id)) elements.set(id, new Element()); return elements.get(id); };
  get('access-code').value = '123456'; get('request-audio').checked = audio;
  get('display-mode').value = '0'; get('quality').value = 'balanced';
  const document = new EventTarget(); Object.assign(document, { body: { dataset: {} }, hidden: false,
    getElementById: get, createElement: () => new Element() });
  const window = new EventTarget();
  const queries = [], timers = new Map(), intervals = new Map(), requests = [], peers = [];
  let nextTimer = 0, paired = true;
  class Peer {
    constructor() { peers.push(this); this.iceGatheringState = 'complete'; this.localDescription = {}; }
    async setRemoteDescription() { this.ontrack({ track: Object.assign(new EventTarget(), { kind: 'video' }) }); }
    getTransceivers() { return []; }
    async createAnswer() { return {}; }
    async setLocalDescription() {}
    async getStats() { return new Map(); }
    close() { this.closed = true; }
  }
  Object.assign(window, { DeskPortI18n: { t: value => value, message: value => typeof value === 'string' ? value : value.literal }, isSecureContext: true, innerWidth: 960, innerHeight: 710, devicePixelRatio: 1,
    RTCPeerConnection: Peer, ResizeObserver: class { observe() {} }, visualViewport: new EventTarget(),
    matchMedia: query => { const value = Object.assign(new EventTarget(), { media: query }); queries.push(value); return value; } });
  if (codecs) window.RTCRtpReceiver = { getCapabilities: kind => ({ codecs: codecs[kind] }) };
  const fetch = async (url, options) => {
    requests.push({ url, body: options.body && JSON.parse(options.body) });
    let data = { ok: true }, status = 200;
    if (url === '/api/resume' || url === '/api/login') {
      if (paired) data = { ok: true, csrfToken: 'synthetic-token', paired: true, pairing: { id: 'synthetic' } };
      else { status = 401; data = { ok: false, code: 'unpaired' }; }
    }
    if (url === '/api/status') data.capabilities = { resize: true, zoomChoices: [1] };
    if (url === '/api/session/start') Object.assign(data, { type: 'offer', sdp: 'synthetic-offer' });
    return { ok: status === 200, status, json: async () => data };
  };
  const context = vm.createContext({ window, document, navigator: { userAgent: 'fixture', platform: 'fixture' },
    location: { protocol: 'https:' }, localStorage: { getItem() {}, setItem() {} }, fetch,
    RTCPeerConnection: Peer, ResizeObserver: window.ResizeObserver, MediaStream: class { addTrack() {} },
    AbortController, TextEncoder, performance, queueMicrotask, setTimeout: (fn, ms) => { const id = ++nextTimer; timers.set(id, { fn, ms }); return id; },
    clearTimeout: id => timers.delete(id), setInterval: fn => { const id = ++nextTimer; intervals.set(id, fn); return id; },
    clearInterval: id => intervals.delete(id) });
  vm.runInContext(source, context); await tick();
  const dispatch = (target, type) => target.dispatchEvent(new Event(type, { cancelable: true }));
  const connect = async () => { dispatch(get('login-form'), 'submit'); await tick(); await tick(); };
  const disconnect = async () => { dispatch(get('disconnect'), 'click'); await tick(); };
  const runResize = async () => { for (const [id, timer] of [...timers]) if (timer.ms <= 800) { timers.delete(id); timer.fn(); } await tick(); };
  return { get, document, window, requests, peers, queries, connect, disconnect, dispatch, runResize,
    heartbeat: async () => { for (const callback of intervals.values()) await callback(); await tick(); },
    revoke: () => { paired = false; } };
}

// A site may be configured to block even silent autoplay. Recovery needs a button
// in audio-free sessions too; no browser policy is disabled by these tests.
for (const audio of [false, true]) {
  let blocked = true;
  const f = await fixture({ audio, play: async () => { if (blocked) throw new DOMException('gesture required', 'NotAllowedError'); } });
  await f.connect();
  check(`blocked autoplay exposes a gesture with audio=${audio}`, !f.get('resume-audio').hidden && f.get('resume-audio').textContent.includes('Tap to play video'));
  check('blocked autoplay never claims streaming', f.get('connection-status').dataset.state === 'connecting');
  blocked = false; f.dispatch(f.get('resume-audio'), 'click'); await tick();
  check('user gesture recovers playback', f.get('connection-status').dataset.state === 'streaming' && f.get('resume-audio').hidden);
  await f.disconnect();
}
{
  const f = await fixture({ play: async video => { if (!video.muted) throw new DOMException('gesture required', 'NotAllowedError'); } });
  await f.connect();
  check('rejected sound preserves silent video and audio prompt', f.get('connection-status').dataset.state === 'streaming' && f.get('desktop-video').muted && !f.get('resume-audio').hidden);
  await f.disconnect();
}
{
  let rejectPlay;
  const f = await fixture({ play: () => new Promise((_, reject) => { rejectPlay = reject; }) });
  await f.connect(); await f.disconnect();
  rejectPlay(new DOMException('late policy decision', 'NotAllowedError')); await tick();
  check('late playback rejection cannot revive stopped media or prompt', f.get('connection-status').dataset.state === 'idle' && f.get('desktop-video').srcObject === null && f.get('resume-audio').hidden);
}
{
  const f = await fixture({ play: async () => { throw new DOMException('decoder unavailable', 'NotSupportedError'); } });
  await f.connect(); await tick();
  check('unsupported decoder reports codec guidance and releases reservation', f.get('connection-status').dataset.state === 'idle' && f.get('notice').textContent.includes('H.264') && f.requests.some(r => r.url === '/api/session/stop'));
}
{
  const f = await fixture(); await f.connect(); await f.runResize();
  f.window.devicePixelRatio = 2; f.dispatch(f.queries.at(-1), 'change'); await f.runResize();
  check('DPR-only change reports backing ratio without a CSS resize', f.requests.filter(r => r.url === '/api/session/resize').at(-1)?.body.viewport.ratio === 2);
  f.window.devicePixelRatio = 1; f.dispatch(f.queries.at(-1), 'change'); await f.runResize();
  check('DPR watcher rearms for the return display', f.requests.filter(r => r.url === '/api/session/resize').at(-1)?.body.viewport.ratio === 1);
  const before = f.requests.length; f.dispatch(f.queries[0], 'change'); await f.runResize();
  check('old density query no longer triggers requests', f.requests.length === before);
  f.document.hidden = true; f.window.devicePixelRatio = 2;
  f.dispatch(f.queries.at(-1), 'change'); await f.runResize();
  f.document.hidden = false; f.dispatch(f.document, 'visibilitychange'); await f.runResize();
  check('foreground return resamples density changes deferred while hidden', f.requests.filter(r => r.url === '/api/session/resize').at(-1)?.body.viewport.ratio === 2);
  f.dispatch(f.window, 'blur'); await tick();
  check('window blur sends release before further input', f.requests.some(r => r.url === '/api/session/input' && r.body.event.type === 'release'));
  f.get('remote-text').value = 'synthetic draft'; f.dispatch(f.get('remote-text'), 'compositionstart');
  await f.disconnect(); await f.connect();
  f.get('remote-text').value = 'synthetic new session'; f.dispatch(f.get('text-panel'), 'submit'); await tick();
  check('interrupted composition does not block text in next session', f.requests.some(r => r.body?.event?.text === 'synthetic new session'));
  f.get('desktop-video').error = { code: 3 };
  f.dispatch(f.get('desktop-video'), 'error'); await tick();
  check('media decode failure stops and clears the stream', f.get('desktop-video').srcObject === null && f.get('notice').textContent.includes('H.264'));
}
{
  const f = await fixture({ codecs: { video: [], audio: [] } }); await f.connect();
  check('missing H264 capability fails before reserving media', f.get('notice').textContent.includes('H.264') && !f.requests.some(r => r.url === '/api/session/start'));
}
{
  const f = await fixture({ codecs: { video: [{ mimeType: 'video/H264' }], audio: [] } }); await f.connect();
  check('missing Opus explains the explicit audio-free fallback', f.get('notice').textContent.includes('Opus') && !f.requests.some(r => r.url === '/api/session/start'));
}
{
  const f = await fixture({ audio: false, codecs: { video: [{ mimeType: 'video/H264' }], audio: [] } }); await f.connect();
  check('missing Opus still allows an explicitly silent session', f.get('connection-status').dataset.state === 'streaming');
  await f.disconnect();
}
{
  const f = await fixture(); await f.connect();
  f.peers[0].ondatachannel({ channel: { label: 'input', readyState: 'open', bufferedAmount: 65536 } });
  f.dispatch(f.window, 'blur'); await tick(); await tick();
  check('backpressured blur release terminates media instead of leaving keys held', f.get('connection-status').dataset.state === 'idle' && f.requests.some(r => r.url === '/api/session/stop'));
}
console.log(JSON.stringify({ passed: true, count: checks.length, checks,
  scope: 'Deterministic production-JS API doubles only; no native browser/host acceptance.' }, null, 2));
