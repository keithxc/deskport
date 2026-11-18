#!/usr/bin/env node
// Isolated browser UI/WebRTC contract test. Never connects to a DeskPort host or injects OS input.
// Install playwright-core in a disposable build directory, then set DESKPORT_PLAYWRIGHT_MODULE
// to its module path and DESKPORT_TEST_CHROME to a Chromium executable. Set DESKPORT_TEST_RTC
// to rtc-transport-test and DESKPORT_TEST_H264 to a Constrained Baseline Level 3.1
// 1280x720 at 30 fps AUD-delimited test pattern. ffprobe verifies the actual bitstream.
// No production Node dependency, native capture, or native input backend is involved.
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import https from 'node:https';
import { execFileSync, spawn, spawnSync } from 'node:child_process';
import { launchTrustedChromium, chromiumSandboxStatus } from './browser-test-context.mjs';

const require = createRequire(import.meta.url);
const { chromium } = require(process.env.DESKPORT_PLAYWRIGHT_MODULE || 'playwright-core');
const nativeExecutable = process.env.DESKPORT_TEST_RTC;
const h264Fixture = process.env.DESKPORT_TEST_H264;
assert.ok(nativeExecutable && h264Fixture, 'Set DESKPORT_TEST_RTC to the isolated rtc-transport-test and DESKPORT_TEST_H264 to an AUD-delimited H.264 test pattern.');
const fixtureProbe = JSON.parse(execFileSync(process.env.DESKPORT_TEST_FFPROBE || 'ffprobe', [
  '-v', 'error', '-select_streams', 'v:0', '-show_entries', 'stream=codec_name,profile,level,width,height', '-of', 'json', h264Fixture
], { encoding: 'utf8' })).streams[0];
assert.equal(fixtureProbe?.codec_name, 'h264', 'The native fixture must contain actual H.264.');
assert.equal(fixtureProbe?.profile, 'Constrained Baseline', 'Do not feed a High-profile bitstream under a Baseline SDP.');
// The host encodes the desktop at its own size; the level follows the macroblock count
// while the SDP keeps 42e01f. Any fixture size is accepted when its level matches.
const fixtureMacroblocks = Math.ceil(fixtureProbe?.width / 16) * Math.ceil(fixtureProbe?.height / 16);
assert.equal(fixtureProbe?.level, fixtureMacroblocks <= 3600 ? 31 : fixtureMacroblocks <= 8192 ? 41 : 51,
  'The fixture level must match the host level for its size.');
// Raw Annex B has no container timestamps; verify its progressive SPS timing instead
// of ffprobe's demuxer rate guess (some FFmpeg versions report twice the frame rate).
const headers = spawnSync(process.env.DESKPORT_TEST_FFMPEG || 'ffmpeg', [
  '-hide_banner', '-i', h264Fixture, '-c:v', 'copy', '-bsf:v', 'trace_headers', '-frames:v', '1', '-f', 'null', '-'
], { encoding: 'utf8' });
assert.equal(headers.status, 0, 'The fixture SPS must be readable.');
const tickUnits = Number(headers.stderr.match(/num_units_in_tick[^\n]*=\s*(\d+)/)?.[1]);
const timeScale = Number(headers.stderr.match(/time_scale[^\n]*=\s*(\d+)/)?.[1]);
assert.equal(timeScale / (2 * tickUnits), 30, 'The validated progressive H.264 fixture timing is 30 fps.');
fixtureProbe.framesPerSecond = 30;
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const output = process.env.DESKPORT_FRONTEND_TEST_OUTPUT || path.join(process.env.DESKPORT_BUILD_ROOT || path.join(process.env.HOME, 'mygit/build/deskport'), 'browser-client/frontend');
await mkdir(output, { recursive: true });
const cert = path.join(output, 'fixture-cert.pem');
const key = path.join(output, 'fixture-key.pem');
const certutil = process.env.DESKPORT_TEST_CERTUTIL;
assert.ok(certutil && process.platform === 'linux', 'Supply DESKPORT_TEST_CERTUTIL for private Linux NSS trust; TLS bypass is not supported.');
execFileSync('openssl', ['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', key, '-out', cert, '-days', '1',
  '-subj', '/CN=DeskPort isolated test root', '-addext', 'subjectAltName=IP:127.0.0.1',
  '-addext', 'basicConstraints=critical,CA:TRUE', '-addext', 'keyUsage=critical,keyCertSign,digitalSignature,keyEncipherment'], { stdio: 'ignore' });
const resources = new Map(await Promise.all(['index.html', 'app.js', 'i18n.js', 'i18n-data.js', 'style.css'].map(async name => [name, await readFile(path.join(repo, 'app/browser', name))])));
resources.set('index.html', Buffer.from(resources.get('index.html').toString().replace('@DESKPORT_VERSION@', '0.7.0-test')));
resources.set('icon.svg', await readFile(path.join(repo, 'app/res/deskport.svg')));
const server = https.createServer({ key: await readFile(key), cert: await readFile(cert) }, (request, response) => {
  const name = request.url === '/' ? 'index.html' : request.url.slice(1);
  if (!resources.has(name)) { response.writeHead(404); response.end(); return; }
  response.setHeader('Content-Type', { 'index.html': 'text/html; charset=utf-8', 'app.js': 'text/javascript; charset=utf-8', 'i18n.js': 'text/javascript; charset=utf-8', 'i18n-data.js': 'text/javascript; charset=utf-8', 'style.css': 'text/css', 'icon.svg': 'image/svg+xml' }[name]);
  response.setHeader('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; media-src 'self' blob:; img-src 'self' data:; object-src 'none'; frame-ancestors 'none'");
  response.end(resources.get(name));
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = `https://127.0.0.1:${server.address().port}`;
let trusted, browser;
try {
  trusted = await launchTrustedChromium({ chromium, executablePath: process.env.DESKPORT_TEST_CHROME,
    certutil, certificate: cert, output });
  browser = trusted.browser;
} catch (error) { await new Promise(resolve => server.close(resolve)); throw error; }
const context = await browser.newContext({ ignoreHTTPSErrors: false, viewport: { width: 1180, height: 820 }, hasTouch: true, locale: 'zh-CN' });
await context.grantPermissions(['local-network-access'], { origin });
const page = await context.newPage();
await context.addInitScript(() => {
  const NativePeer = window.RTCPeerConnection;
  window.fixturePeers = [];
  window.RTCPeerConnection = class extends NativePeer {
    constructor(...args) {
      super(...args);
      window.fixturePeers.push(this);
      this.fixtureStates = [];
      this.addEventListener('iceconnectionstatechange', () => this.fixtureStates.push(this.iceConnectionState));
      this.addEventListener('datachannel', event => { window.fixtureChannel = event.channel; });
    }
  };
  const nativeSend = RTCDataChannel.prototype.send;
  RTCDataChannel.prototype.send = function(message) {
    window.fixtureHost?.events.push(JSON.parse(message));
    return nativeSend.call(this, message);
  };
});
const errors = [];
page.on('pageerror', error => errors.push(error.message));
const requests = [];
let mode = 'bad-code';
let serverPairing = null;
let tailnetOwner = false;
let sessionToken = '';
let sessionCounter = 0;
let offer;
let native;
let signalDirectory;
let nativeOutput = '';
let heldStop = null;
const checks = [];
let sandboxStatus = null;
const check = (name, value = true) => { assert.ok(value, name); checks.push(name); };

await context.route('**/api/**', async route => {
  const request = route.request();
  const endpoint = new URL(request.url()).pathname;
  const body = request.postDataJSON();
  requests.push({ endpoint, body, token: request.headers()['x-deskport-session'] || '' });
  const respond = (status, json) => route.fulfill({ status, contentType: 'application/json', body: JSON.stringify(json) });
  const authenticated = (paired, reuse = false) => {
    if (!reuse || !sessionToken) sessionToken = `fixture-only-memory-token-${++sessionCounter}`;
    return route.fulfill({ status: 200, contentType: 'application/json', headers: {
      'Set-Cookie': `__Host-deskport-session=fixture-${sessionCounter}; Secure; HttpOnly; SameSite=Strict; Path=/`
    }, body: JSON.stringify({ ok: true, csrfToken: sessionToken, paired, pairing: paired ? serverPairing : null }) });
  };
  if (endpoint === '/api/resume') {
    if (mode === 'resume-offline') return route.abort('failed');
    if (mode === 'slow-resume') await new Promise(resolve => setTimeout(resolve, 350));
    if (!serverPairing) return respond(401, { ok: false, code: 'unpaired' });
    return authenticated(true, true);
  }
  if (endpoint === '/api/login') {
    if (mode === 'bad-code') return respond(401, { ok: false, code: 'unauthorized' });
    if (mode === 'locked') return respond(429, { ok: false, code: 'code-locked' });
    if (body.remember) serverPairing = { id: 'fixture-pairing', name: body.deviceName || '测试浏览器' };
    return authenticated(Boolean(body.remember));
  }
  if (endpoint === '/api/login/tailnet') {
    if (!tailnetOwner) return respond(403, { ok: false, code: 'tailnet-unavailable' });
    if (body.probe) return respond(200, { ok: true, tailnet: true });
    return authenticated(false);
  }
  if (!sessionToken || request.headers()['x-deskport-session'] !== sessionToken) return respond(401, { ok: false, code: 'unauthorized' });
  if (endpoint === '/api/forget') {
    if (mode === 'forget-failed') return respond(503, { ok: false, code: 'storage-unavailable' });
    serverPairing = null; sessionToken = '';
    return respond(200, { ok: true });
  }
  if (endpoint === '/api/logout') {
    if (mode === 'slow-logout') await new Promise(resolve => setTimeout(resolve, 350));
    sessionToken = ''; return respond(200, { ok: true });
  }
  if (endpoint === '/api/status') return respond(200, { ok: true, authenticated: true, sharing: true, busy: mode === 'busy', mediaAvailable: true, hostName: '测试电脑',
    capabilities: { resize: true, adaptiveDisplay: true, extendDisplay: false, zoomChoices: [0.8, 1, 1.2, 1.5] } });
  if (endpoint === '/api/session/start') {
    if (mode === 'slow-start') await new Promise(resolve => setTimeout(resolve, 500));
    return respond(200, { ok: true, ...offer });
  }
  if (endpoint === '/api/session/answer') {
    await writeFile(path.join(signalDirectory, 'answer.json'), JSON.stringify(body));
    return respond(200, { ok: true });
  }
  if (endpoint === '/api/session/heartbeat' && mode === 'expired') {
    serverPairing = null; sessionToken = '';
    return respond(401, { ok: false, code: 'unauthorized' });
  }
  if (endpoint === '/api/session/resize') return respond(200, { ok: true, width: 1600, height: 900 });
  if (endpoint === '/api/session/input') await page.evaluate(event => window.fixtureHost?.events.push(event), body.event);
  if (endpoint === '/api/session/stop') {
    if (native) { native.kill('SIGTERM'); native = null; }
    if (heldStop) { heldStop.processed(); await heldStop.promise; }
  }
  return respond(200, { ok: true });
});

async function waitVisible(selector) { await page.locator(selector).waitFor({ state: 'visible' }); }
async function login() { await page.locator('#access-code').fill('123456'); await page.locator('#connect').click(); }
async function inputEvents() { return page.evaluate(() => window.fixtureHost.events); }
async function waitEvent(type, predicate = {}) {
  await page.waitForFunction(({ type, predicate }) => window.fixtureHost.events.some(event => event.type === type && Object.entries(predicate).every(([key, value]) => event[key] === value)), { type, predicate });
}
async function prepareHost() {
  signalDirectory = path.join(output, `client-signal-${Date.now()}`);
  await mkdir(signalDirectory, { recursive: true });
  native = spawn(nativeExecutable, ['--browser', signalDirectory, h264Fixture]);
  native.stdout.on('data', data => { nativeOutput += data; });
  native.stderr.on('data', data => { nativeOutput += data; });
  offer = null;
  for (let attempt = 0; attempt < 100; attempt++) {
    try { offer = JSON.parse(await readFile(path.join(signalDirectory, 'offer.json'), 'utf8')); break; } catch (_) { /* Wait for complete ICE offer. */ }
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  assert.ok(offer, `Native fixture did not produce an offer: ${nativeOutput}`);
  assert.match(offer.sdp, /profile-level-id=42e01f/i, 'The current native transport must advertise Constrained Baseline level 3.1.');
  await page.evaluate(() => { window.fixtureHost = { events: [] }; });
}

try {
  {
    const sandbox = await chromiumSandboxStatus(context);
    sandboxStatus = { namespace: sandbox.namespace, seccomp: sandbox.seccomp };
    await writeFile(path.join(output, 'sandbox-status.txt'), sandbox.text);
    check('Chromium namespace and seccomp sandboxes remain enabled', sandbox.namespace && sandbox.seccomp);
    check('browser command has no TLS, sandbox, web-security or autoplay-policy bypass', sandbox.noBypassFlags);
    const rogueKey = path.join(output, 'untrusted-key.pem'), rogueCert = path.join(output, 'untrusted-cert.pem');
    execFileSync('openssl', ['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', rogueKey, '-out', rogueCert,
      '-days', '1', '-subj', '/CN=Untrusted fixture', '-addext', 'subjectAltName=IP:127.0.0.1'], { stdio: 'ignore' });
    const rogue = https.createServer({ key: await readFile(rogueKey), cert: await readFile(rogueCert) }, (_, response) => response.end('untrusted fixture'));
    await new Promise(resolve => rogue.listen(0, '127.0.0.1', resolve));
    const probe = await context.newPage();
    try {
      await assert.rejects(probe.goto(`https://127.0.0.1:${rogue.address().port}`), /ERR_CERT_AUTHORITY_INVALID/);
      check('untrusted certificate fails with browser TLS validation enabled');
    } finally { await probe.close(); await new Promise(resolve => rogue.close(resolve)); }
  }
  await page.goto(origin);
  check('fixture loads using its private NSS trust anchor', await page.evaluate(() => isSecureContext));
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'idle');
  check('first visit checks pairing without starting media', requests.some(request => request.endpoint === '/api/resume') && !requests.some(request => request.endpoint === '/api/session/start'));
  check('remember browser is off by default (7 days when chosen)', !(await page.locator('#remember-browser').isChecked()) &&
    (await page.locator('label.check').first().textContent()).includes('7 天'));
  check('code field asks for six digits with a numeric keyboard', await page.locator('#access-code').evaluate(input =>
    input.inputMode === 'numeric' && input.autocomplete === 'one-time-code' && input.type === 'text'));
  check('page logo and browser tab use the DeskPort icon', await page.evaluate(() => {
    const logo = document.querySelector('.brand-mark');
    return logo.complete && logo.naturalWidth > 0 && document.querySelector('link[rel="icon"]').getAttribute('href') === '/icon.svg';
  }));
  const codecs = await page.evaluate(() => RTCRtpReceiver.getCapabilities('video').codecs.filter(codec => codec.mimeType.toLowerCase() === 'video/h264'));
  await writeFile(path.join(output, 'chrome-h264-capabilities.json'), JSON.stringify({ browser: browser.version(), codecs }, null, 2));
  await page.screenshot({ path: path.join(output, 'login-desktop.png') });
  await login();
  await page.waitForFunction(() => document.getElementById('notice').textContent.includes('动态码不正确'));
  check('incorrect access code shows an actionable error');
  check('failed login clears the code', await page.locator('#access-code').inputValue() === '');
  mode = 'locked';
  await login();
  await page.waitForFunction(() => document.getElementById('notice').textContent.includes('暂停 15 分钟'));
  check('repeated wrong codes explain the 15-minute pause');
  mode = 'busy';
  await page.locator('#remember-browser').uncheck();
  await login();
  await page.waitForFunction(() => document.getElementById('notice').textContent.includes('其他连接'));
  check('busy host does not start or take over a session', !requests.some(request => request.endpoint === '/api/session/start'));
  mode = 'success';
  await page.locator('#remember-browser').check();
  await page.locator('#device-name').fill('我的测试浏览器');
  await prepareHost();
  await login();
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'streaming', null, { timeout: 25000 });
  await page.waitForFunction(() => window.fixtureChannel.readyState === 'open');
  check('actual native WebRTC H.264 video renders', await page.evaluate(size => document.getElementById('desktop-video').videoWidth === size.width && document.getElementById('desktop-video').videoHeight === size.height, { width: fixtureProbe.width, height: fixtureProbe.height }));
  const authBeforeLanguage = requests.filter(request => ['/api/login', '/api/resume', '/api/logout', '/api/forget', '/api/session/start', '/api/session/stop'].includes(request.endpoint)).length;
  await page.selectOption('#language', 'ar');
  await page.selectOption('#language', 'zh-Hans');
  check('language changes keep the native stream and authentication intact', await page.locator('#connection-status').getAttribute('data-state') === 'streaming' &&
    requests.filter(request => ['/api/login', '/api/resume', '/api/logout', '/api/forget', '/api/session/start', '/api/session/stop'].includes(request.endpoint)).length === authBeforeLanguage &&
    await page.locator('#desktop-video').evaluate(video => video.videoWidth > 0 && !video.paused));
  const codecStats = await page.evaluate(async () => {
    const stats = await window.fixturePeers[0].getStats();
    const result = [];
    stats.forEach(report => { if (report.type === 'inbound-rtp' && report.packetsReceived > 0) result.push(stats.get(report.codecId)?.mimeType); });
    return result;
  });
  check('native test-pattern stream negotiates H.264', codecStats.includes('video/H264'));
  check('receive-only audio negotiates Opus', codecStats.includes('audio/opus'));
  const start = requests.find(request => request.endpoint === '/api/session/start').body;
  check('stream hint stays inside validated dimensions', start.width <= 1280 && start.height <= 720 && start.fps === 60 && start.bitrateKbps <= 14000 && start.width % 4 === 0 && start.height % 4 === 0);
  check('H.264 capabilities sent for server negotiation', start.videoCapabilities.length > 0);
  check('start reports the drawable viewport for shared workspace policy',
    start.viewport?.width > 0 && start.viewport?.height > 0 && start.viewport?.ratio > 0 && start.zoom === 1 && start.displayPolicy === 0);
  check('code does not enter URL', !page.url().includes('123456'));
  check('credentials not persisted in web storage', await page.evaluate(() => localStorage.length === 1 && localStorage.getItem('deskport.language') === 'zh-Hans' && sessionStorage.length === 0));
  const otherTab = await context.newPage();
  await otherTab.goto(origin);
  await otherTab.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'idle' && !document.getElementById('paired-ready').hidden);
  const stopsBeforeIdleTab = requests.filter(request => ['/api/session/stop', '/api/logout'].includes(request.endpoint)).length;
  const tokenBeforeOtherConnect = sessionToken;
  mode = 'busy';
  await otherTab.locator('#connect').click();
  await otherTab.waitForFunction(() => document.getElementById('notice').textContent.includes('其他连接'));
  check('busy paired tab cannot stop or log out the owner', requests.filter(request => ['/api/session/stop', '/api/logout'].includes(request.endpoint)).length === stopsBeforeIdleTab && sessionToken === tokenBeforeOtherConnect);
  mode = 'success';
  await otherTab.reload();
  await otherTab.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'idle' && !document.getElementById('paired-ready').hidden);
  await otherTab.goto('about:blank');
  await otherTab.close({ runBeforeUnload: true });
  await page.waitForTimeout(150);
  check('closing a restored idle tab leaves owner session intact', requests.filter(request => ['/api/session/stop', '/api/logout'].includes(request.endpoint)).length === stopsBeforeIdleTab && Boolean(native));
  check('other-tab restore does not interrupt video', await page.locator('#connection-status').getAttribute('data-state') === 'streaming');
  await page.locator('#right-click').click();
  await waitEvent('button', { button: 3, down: false });
  check('right click reaches ordered data channel');
  await page.locator('#viewport').focus();
  await page.keyboard.down('Control'); await page.keyboard.press('KeyA'); await page.keyboard.up('Control');
  await waitEvent('key', { key: 65, down: true, modifiers: 2 });
  await waitEvent('key', { key: 17, down: false });
  check('hardware key and modifier press/release reach the host');
  await page.keyboard.down('Shift');
  await page.locator('#keyboard-toggle').click();
  await page.keyboard.up('Shift');
  await waitEvent('release');
  check('leaving desktop focus releases held input');
  await page.locator('#remote-text').dispatchEvent('compositionstart', { data: '中' });
  await page.locator('#remote-text').fill('中文输入测试');
  await page.locator('#send-text').click();
  check('unfinished IME composition is not sent', !(await inputEvents()).some(event => event.type === 'text'));
  await page.locator('#remote-text').dispatchEvent('compositionend', { data: '中文输入测试' });
  await page.locator('#send-text').click();
  await waitEvent('text', { text: '中文输入测试' });
  check('Chinese text sends once after composition');
  await page.locator('#keyboard-toggle').click();
  await page.locator('#shortcuts-toggle').click();
  await page.locator('[data-keys="91,67"]').click();
  await waitEvent('key', { key: 67, down: true, modifiers: 8 });
  check('on-screen system shortcut reaches host');
  await page.locator('#shortcuts-toggle').click();
  const area = await page.locator('#viewport').boundingBox();
  await page.mouse.move(area.x + area.width / 2, area.y + area.height / 2);
  await page.mouse.wheel(0, 120);
  await waitEvent('scroll', { y: -120 });
  check('wheel delta maps to host direction and units');
  const cdp = await context.newCDPSession(page);
  const point = { x: area.x + 180, y: area.y + 160 };
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [point] });
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: point.x + 35, y: point.y + 18 }] });
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
  await waitEvent('relative');
  check('touch trackpad moves remote pointer');
  await page.locator('#touch-mode').selectOption('direct');
  await page.touchscreen.tap(point.x, point.y);
  await waitEvent('button', { button: 1, down: false });
  check('direct touch sends balanced button events');
  await page.setViewportSize({ width: 820, height: 1180 });
  await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
  check('tablet toolbar remains inside viewport', await page.locator('.toolbar').evaluate(element => element.getBoundingClientRect().bottom <= innerHeight));
  await page.screenshot({ path: path.join(output, 'session-ipad-layout.png') });
  check('tablet layout has no horizontal overflow', await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
  check('desktop header matches native height and inline build version', await page.locator('.app-header').evaluate(element =>
    element.getBoundingClientRect().height === 56 && element.querySelector('.app-version').textContent === 'v0.7.0-test'));
  await page.setViewportSize({ width: 320, height: 568 });
  await page.locator('#host-name').evaluate(element => { element.textContent = 'An isolated test computer with a very long name'; });
  check('smallest phone keeps language, long host name and disconnect inside header', await page.locator('.app-header').evaluate(element => {
    const host = element.querySelector('#host-name'), button = element.querySelector('#disconnect');
    return element.getBoundingClientRect().height >= 52 && element.getBoundingClientRect().height <= 120 && host.scrollWidth > host.clientWidth &&
      button.getBoundingClientRect().right <= innerWidth && document.documentElement.scrollWidth <= innerWidth;
  }));
  await page.screenshot({ path: path.join(output, 'session-phone-320.png') });
  await page.setViewportSize({ width: 390, height: 844 });
  await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
  check('phone toolbar remains inside viewport', await page.locator('.toolbar').evaluate(element => element.getBoundingClientRect().bottom <= innerHeight));
  check('narrow layout has no horizontal overflow', await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
  const resizeRequests = () => requests.filter(request => request.endpoint === '/api/session/resize');
  for (let i = 0; i < 40 && !resizeRequests().some(request => request.body.viewport.width < 400); ++i) await new Promise(resolve => setTimeout(resolve, 100));
  const phoneResize = resizeRequests().at(-1)?.body;
  check('window size changes resize the remote desktop once settled', phoneResize?.viewport.width < 400 && phoneResize.viewport.height > phoneResize.viewport.width);
  const resizesBeforeZoom = resizeRequests().length;
  await page.selectOption('#session-zoom', '1.2');
  for (let i = 0; i < 20 && resizeRequests().length === resizesBeforeZoom; ++i) await new Promise(resolve => setTimeout(resolve, 100));
  check('in-session content size applies through live resize', resizeRequests().at(-1)?.body.zoom === 1.2);
  const beforeDensity = resizeRequests().length;
  await cdp.send('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 2, mobile: false });
  await page.waitForFunction(() => devicePixelRatio === 2);
  // CDP changes the sampled DPR but does not emulate a physical monitor's
  // resolution media-query event. Supply that resize notification explicitly;
  // the query's rearming is covered by test-browser-compat.mjs.
  await page.evaluate(() => window.dispatchEvent(new Event('resize')));
  for (let i = 0; i < 30 && resizeRequests().length === beforeDensity; ++i) await new Promise(resolve => setTimeout(resolve, 100));
  check('emulated DPR reaches live resize after browser notification', resizeRequests().at(-1)?.body.viewport.ratio === 2);
  await cdp.send('Emulation.clearDeviceMetricsOverride');
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'streaming');
  await new Promise(resolve => setTimeout(resolve, 5200));
  check('static sessions send authenticated heartbeat', requests.some(request => request.endpoint === '/api/session/heartbeat'));
  const mediaStats = await page.evaluate(async () => {
    const stats = await window.fixturePeers[0].getStats();
    return [...stats.values()].filter(report => report.type === 'inbound-rtp').map(report => ({
      kind: report.kind, packetsReceived: report.packetsReceived, framesDecoded: report.framesDecoded,
      framesPerSecond: report.framesPerSecond, bytesReceived: report.bytesReceived, codec: stats.get(report.codecId)
    }));
  });
  check('H.264 decode continues beyond the first frame', mediaStats.some(report => report.kind === 'video' && report.framesDecoded > 30));
  await page.evaluate(() => window.fixtureChannel.close());
  await page.waitForFunction(() => window.fixtureChannel.readyState === 'closed');
  await page.locator('#right-click').click();
  await page.waitForFunction(() => window.fixtureHost.events.filter(event => event.type === 'button' && event.button === 3 && !event.down).length >= 2);
  const fallbackButtons = requests.filter(request => request.endpoint === '/api/session/input' && request.body.event.type === 'button').map(request => request.body.event.down);
  check('HTTP input fallback preserves down/up ordering', fallbackButtons.length >= 2 && fallbackButtons.at(-2) === true && fallbackButtons.at(-1) === false);
  const nextOwner = await context.newPage();
  await nextOwner.goto(origin);
  await nextOwner.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'idle' && !document.getElementById('paired-ready').hidden);
  let stopProcessed;
  let releaseStop;
  const stopProcessedPromise = new Promise(resolve => { stopProcessed = resolve; });
  heldStop = { processed: stopProcessed, promise: new Promise(resolve => { releaseStop = resolve; }) };
  const logoutsBeforeDisconnect = requests.filter(request => request.endpoint === '/api/logout').length;
  await page.locator('#disconnect').click();
  await stopProcessedPromise;
  check('disconnect waits for its own stop response', await page.locator('#connect').isDisabled());
  await prepareHost();
  await nextOwner.locator('#connect').click();
  await nextOwner.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'streaming', null, { timeout: 25000 });
  const nextOwnerFrames = () => nextOwner.evaluate(async () => {
    const stats = await window.fixturePeers[0].getStats();
    return [...stats.values()].find(report => report.type === 'inbound-rtp' && report.kind === 'video')?.framesDecoded || 0;
  });
  const framesBeforeLateCleanup = await nextOwnerFrames();
  releaseStop(); heldStop = null;
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'idle');
  await nextOwner.waitForTimeout(450);
  check('paired disconnect stops only owned media and never logs out shared session', requests.some(request => request.endpoint === '/api/session/stop') && requests.filter(request => request.endpoint === '/api/logout').length === logoutsBeforeDisconnect);
  check('late first-tab cleanup leaves the new owner streaming', await nextOwnerFrames() > framesBeforeLateCleanup && await nextOwner.locator('#connection-status').getAttribute('data-state') === 'streaming');
  await nextOwner.locator('#disconnect').click();
  await nextOwner.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'idle');
  // Short-session expiry is distinct from persistent pairing revocation.
  const previousToken = sessionToken;
  sessionToken = '';
  mode = 'busy';
  await nextOwner.locator('#connect').click();
  await nextOwner.waitForFunction(() => document.getElementById('notice').textContent.includes('其他连接'));
  check('idle paired tab resumes after short-session expiry', Boolean(sessionToken) && sessionToken !== previousToken);
  await nextOwner.close();
  mode = 'success';
  check('disconnect keeps paired ready without requiring a code', await page.locator('#paired-ready').isVisible() && await page.locator('#credential-fields').isHidden());
  check('remember and safe device name are sent only at pairing', requests.filter(request => request.endpoint === '/api/login').at(-1).body.remember === true && serverPairing.name === '我的测试浏览器');
  mode = 'forget-failed';
  const resumesBeforeForget = requests.filter(request => request.endpoint === '/api/resume').length;
  await page.locator('#forget-pairing').click();
  await page.waitForFunction(() => document.getElementById('notice').textContent === DeskPortI18n.message({ source: 'Could not confirm pairing removal. %1', args: ['The computer could not save pairing changes. Try again later.'] }));
  check('forget after disconnect resumes a fresh short session first', requests.filter(request => request.endpoint === '/api/resume').length === resumesBeforeForget + 1);
  check('failed forget retains pairing and reports failure', await page.locator('#paired-ready').isVisible() && Boolean(serverPairing));
  mode = 'success';
  const startsBeforeReload = requests.filter(request => request.endpoint === '/api/session/start').length;
  await page.reload();
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'idle' && !document.getElementById('paired-ready').hidden);
  check('refresh restores pairing without automatically streaming', requests.filter(request => request.endpoint === '/api/session/start').length === startsBeforeReload);
  check('remembered device name is safely rendered', await page.locator('#paired-name').textContent() === '我的测试浏览器');
  mode = 'resume-offline';
  await page.reload();
  await page.waitForFunction(() => !document.getElementById('retry-pairing').hidden);
  check('offline resume does not claim that pairing was cancelled', !(await page.locator('#notice').textContent()).includes('取消') && Boolean(serverPairing));
  mode = 'slow-resume';
  await page.locator('#retry-pairing').click();
  check('pending resume blocks overlapping connect and forget', await page.locator('#connect').isDisabled());
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'idle' && !document.getElementById('paired-ready').hidden);
  check('retry recovers existing pairing without new code', await page.locator('#access-code').inputValue() === '' && await page.locator('#credential-fields').isHidden());
  mode = 'success';
  await prepareHost();
  await page.locator('#connect').click();
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'streaming', null, { timeout: 25000 });
  check('paired reconnect starts media without re-entering a code', await page.locator('#access-code').inputValue() === '');
  mode = 'expired';
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'idle', null, { timeout: 15000 });
  await page.waitForFunction(() => !document.getElementById('credential-fields').hidden);
  check('host-revoked pairing stops playback and returns to code entry', await page.locator('#notice').textContent().then(text => text.includes('配对')));
  await page.locator('#remember-browser').check();
  mode = 'busy';
  await login();
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'idle' && !document.getElementById('paired-ready').hidden);
  mode = 'success';
  await page.locator('#forget-pairing').click();
  await page.waitForFunction(() => document.getElementById('notice').textContent.includes('已取消此浏览器配对'));
  check('successful forget returns to code entry', !serverPairing && await page.locator('#credential-fields').isVisible() && await page.locator('#paired-ready').isHidden());
  await page.reload();
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'idle');
  check('forgotten browser cannot resume on refresh', await page.locator('#credential-fields').isVisible() && await page.locator('#access-code').inputValue() === '');
  check('reloaded page does not remember by default', !(await page.locator('#remember-browser').isChecked()));
  await page.locator('#remember-browser').check();
  mode = 'slow-start';
  await login();
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'connecting');
  const stopsBeforeCancel = requests.filter(request => request.endpoint === '/api/session/stop').length;
  await page.locator('#disconnect').click();
  check('cancelling pending media start blocks a new overlapping connection', await page.locator('#connect').isDisabled());
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'idle');
  check('cancelled start is stopped after its successful reservation', requests.filter(request => request.endpoint === '/api/session/stop').length === stopsBeforeCancel + 1 && await page.locator('#paired-ready').isVisible());
  mode = 'success';
  await page.locator('#forget-pairing').click();
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'idle' && document.getElementById('paired-ready').hidden);
  await page.locator('#remember-browser').uncheck();
  mode = 'busy';
  await login();
  await page.waitForFunction(() => document.getElementById('notice').textContent.includes('其他连接'));
  check('temporary login does not retain paired UI', !serverPairing && await page.locator('#credential-fields').isVisible() && requests.filter(request => request.endpoint === '/api/login').at(-1).body.remember === false);
  mode = 'slow-start';
  await login();
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'connecting');
  const temporaryLogoutsBefore = requests.filter(request => request.endpoint === '/api/logout').length;
  await page.locator('#disconnect').click();
  mode = 'slow-logout';
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'idle');
  check('temporary owned session still logs out after stopping', requests.filter(request => request.endpoint === '/api/logout').length === temporaryLogoutsBefore + 1 && await page.locator('#credential-fields').isVisible());
  // An owned Tailscale device: no code field, a probe only, then a code-free login.
  mode = 'success'; serverPairing = null; tailnetOwner = true;
  const tailnetFrom = requests.length;
  await page.goto(origin);
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'idle');
  const probes = requests.slice(tailnetFrom).filter(request => request.endpoint === '/api/login/tailnet');
  check('owned Tailscale device hides the access code', !(await page.locator('#credential-fields').isVisible()) &&
    (await page.locator('#login-intro').textContent()).includes('Tailscale'));
  check('Tailscale probe creates no session', probes.length === 1 && probes[0].body.probe === true &&
    !requests.slice(tailnetFrom).some(request => request.endpoint.startsWith('/api/session/')));
  await page.screenshot({ path: path.join(output, 'login-tailnet.png') });
  await prepareHost();
  await page.locator('#connect').click();
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'streaming', null, { timeout: 25000 });
  const tailnetRequests = requests.slice(tailnetFrom);
  check('owned Tailscale device streams without the access code',
    tailnetRequests.filter(request => request.endpoint === '/api/login/tailnet').at(-1).body.probe === undefined &&
    !tailnetRequests.some(request => request.endpoint === '/api/login'));
  await page.locator('#disconnect').click();
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'idle');
  check('after disconnect a Tailscale device can connect again without the code', !(await page.locator('#credential-fields').isVisible()));
  // Inject only the documented play() rejection. The subsequent gesture uses
  // the real browser decoder/transport and leaves browser autoplay policy on.
  await page.evaluate(() => {
    window.fixtureNativePlay = HTMLMediaElement.prototype.play;
    window.fixtureBlockPlay = true;
    HTMLMediaElement.prototype.play = function() {
      if (this.srcObject && window.fixtureBlockPlay) return Promise.reject(new DOMException('Fixture gesture required', 'NotAllowedError'));
      return window.fixtureNativePlay.call(this);
    };
  });
  if (!await page.locator('.connection-options').evaluate(element => element.open)) await page.locator('.connection-options summary').click();
  await page.locator('#request-audio').uncheck();
  await prepareHost();
  await page.locator('#connect').click();
  await waitVisible('#resume-audio');
  check('silent autoplay rejection exposes a manual video gesture', (await page.locator('#resume-audio').textContent()).includes('播放画面'));
  await page.evaluate(() => { window.fixtureBlockPlay = false; });
  await page.locator('#resume-audio').click();
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'streaming', null, { timeout: 25000 });
  check('manual gesture resumes real video in an audio-free session', await page.locator('#desktop-video').evaluate(video => !video.paused && video.muted && video.videoWidth > 0));
  await page.locator('#disconnect').click();
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'idle');
  await page.evaluate(() => { HTMLMediaElement.prototype.play = window.fixtureNativePlay; });
  tailnetOwner = false;
  check('browser reports no uncaught application errors', errors.length === 0);
  const report = { browser: browser.version(), checks, count: checks.length, fixtureProbe,
    sandboxStatus,
    certificateValidation: 'Enabled; short-lived fixture root in private HOME NSS only; untrusted certificate rejected',
    media: 'Real production RtcSession/libdatachannel Constrained Baseline H.264/Opus and DataChannel with a verified test pattern; mocked authentication/server HTTP API. No native capture/input injection or physical iPad validation.', mediaStats, errors };
  await writeFile(path.join(output, 'frontend-test-results.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify(report, null, 2));
} catch (error) {
  console.error(JSON.stringify({ checks, errors, browserState: await page.evaluate(() => ({
    status: document.getElementById('connection-status')?.textContent,
    notice: document.getElementById('notice')?.textContent,
    peers: (window.fixturePeers || []).map(peer => ({ states: peer.fixtureStates, signalingState: peer.signalingState })),
    video: { width: document.getElementById('desktop-video')?.videoWidth, paused: document.getElementById('desktop-video')?.paused }
  })).catch(() => ({ unavailable: true })), endpoints: requests.map(request => request.endpoint) }, null, 2));
  await page.screenshot({ path: path.join(output, 'failure.png') });
  throw error;
} finally {
  if (native) native.kill('SIGTERM');
  await context.close();
  await trusted.close();
  await new Promise(resolve => server.close(resolve));
}
