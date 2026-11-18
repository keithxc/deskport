#!/usr/bin/env node
// Real HTTPS gateway + native host browser acceptance driver. Only use the isolated
// Xvfb/Pulse fixture with forceInputDisabled enabled; never point this at a personal host.
// Usage: node tests/test-browser-native.mjs --fixture /path/to/browser.json
// Fixture fields: url, code, chromium, outputDir. playwright-core is a test-only
// dependency, resolved normally or through DESKPORT_PLAYWRIGHT_MODULE.
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import path from 'node:path';
import net from 'node:net';
import { launchTrustedChromium, chromiumSandboxStatus } from './browser-test-context.mjs';

const fixtureIndex = process.argv.indexOf('--fixture');
assert.ok(fixtureIndex >= 0 && process.argv[fixtureIndex + 1], 'Pass --fixture with an isolated browser.json file.');
const fixture = JSON.parse(await readFile(process.argv[fixtureIndex + 1], 'utf8'));
assert.ok(new URL(fixture.url).protocol === 'https:', 'The fixture must serve production HTTPS.');
assert.ok(['127.0.0.1', 'localhost'].includes(new URL(fixture.url).hostname), 'The supervised fixture must use loopback.');
assert.match(fixture.code, /^[0-9]{6}$/, 'Fixture access code must contain six digits.');
assert.ok(fixture.chromium && fixture.outputDir, 'Fixture must include chromium and outputDir.');
assert.ok(fixture.isolatedSynthetic === true && fixture.nativeInputEnabled === false && fixture.controlSocket,
  'Only a supervised synthetic fixture with native input disabled and a private TOTP control socket is supported.');
assert.ok(fixture.certificate && fixture.certutil, 'The native fixture requires its private certificate and NSS certutil.');
await mkdir(fixture.outputDir, { recursive: true });
const require = createRequire(import.meta.url);
const { chromium } = require(process.env.DESKPORT_PLAYWRIGHT_MODULE || 'playwright-core');
const trusted = await launchTrustedChromium({ chromium, executablePath: fixture.chromium,
  certificate: fixture.certificate, certutil: fixture.certutil, output: fixture.outputDir });
const browser = trusted.browser;
const context = await browser.newContext({ ignoreHTTPSErrors: false, viewport: { width: 1280, height: 890 }, hasTouch: true });
// This grants only the disposable test origin; it does not change the user's profile.
await context.grantPermissions(['local-network-access'], { origin: new URL(fixture.url).origin }).catch(() => {});
const page = await context.newPage();
page.setDefaultTimeout(20000);
const checks = [];
const errors = [];
const apiStatuses = [];
const sessions = [];
const check = (name, value = true) => { assert.ok(value, name); checks.push(name); };
page.on('pageerror', error => errors.push(error.message));
page.on('response', async response => {
  const url = new URL(response.url());
  if (!url.pathname.startsWith('/api/')) return;
  const entry = { path: url.pathname, status: response.status() };
  apiStatuses.push(entry);
  if (response.status() >= 400) {
    try {
      const failure = await response.json();
      // Record only bounded error diagnostics, never login tokens, codes, or SDP bodies.
      if (typeof failure.code === 'string' && /^[a-z][a-z0-9-]{0,63}$/.test(failure.code)) entry.code = failure.code;
      if (typeof failure.message === 'string') entry.message = failure.message.slice(0, 500);
    } catch (_) { /* A non-JSON failure is represented by its HTTP status. */ }
  }
});
await page.addInitScript(() => {
  const NativePeer = window.RTCPeerConnection;
  window.deskportTestPeers = [];
  window.RTCPeerConnection = class extends NativePeer {
    constructor(...args) {
      super(...args);
      window.deskportTestPeers.push(this);
    }
  };
});

async function stats() {
  return page.evaluate(async () => {
    const peer = window.deskportTestPeers.at(-1);
    const reports = await peer.getStats();
    const video = document.getElementById('desktop-video');
    const canvas = document.createElement('canvas');
    canvas.width = 64; canvas.height = 36;
    const drawing = canvas.getContext('2d', { willReadFrequently: true });
    drawing.drawImage(video, 0, 0, canvas.width, canvas.height);
    const pixels = drawing.getImageData(0, 0, canvas.width, canvas.height).data;
    let sum = 0, sumSquared = 0, brightPixels = 0, hash = 2166136261;
    const colors = new Set();
    for (let i = 0; i < pixels.length; i += 4) {
      const intensity = (pixels[i] + pixels[i + 1] + pixels[i + 2]) / 3;
      sum += intensity; sumSquared += intensity * intensity;
      if (intensity > 24) brightPixels++;
      colors.add(`${pixels[i] >> 5},${pixels[i + 1] >> 5},${pixels[i + 2] >> 5}`);
      for (let channel = 0; channel < 3; channel++) hash = Math.imul(hash ^ pixels[i + channel], 16777619) >>> 0;
    }
    const count = pixels.length / 4;
    const samplePoints = [[0.1, 0.1], [0.5, 0.1], [0.9, 0.1], [0.1, 0.5], [0.5, 0.5], [0.9, 0.5], [0.5, 0.9]];
    const samples = samplePoints.map(([x, y]) => {
      const offset = (Math.floor(y * canvas.height) * canvas.width + Math.floor(x * canvas.width)) * 4;
      return { x, y, rgb: Array.from(pixels.slice(offset, offset + 3)) };
    });
    return {
      connectionState: peer.connectionState, iceConnectionState: peer.iceConnectionState,
      video: { width: video.videoWidth, height: video.videoHeight, paused: video.paused, currentTime: video.currentTime },
      pixels: { hash, mean: sum / count, variance: sumSquared / count - (sum / count) ** 2, brightFraction: brightPixels / count, distinctQuantizedColors: colors.size, samples },
      inbound: [...reports.values()].filter(report => report.type === 'inbound-rtp').map(report => ({
        kind: report.kind || report.mediaType, packetsReceived: report.packetsReceived,
        bytesReceived: report.bytesReceived, framesDecoded: report.framesDecoded,
        framesDropped: report.framesDropped, framesPerSecond: report.framesPerSecond,
        totalAudioEnergy: report.totalAudioEnergy, totalSamplesReceived: report.totalSamplesReceived,
        audioLevel: report.audioLevel, codec: reports.get(report.codecId)
      }))
    };
  });
}

async function connectAndMeasure(number) {
  // This media-only regression intentionally exercises a fresh temporary login
  // each time; durable pairing has its own real-profile acceptance driver.
  if (number > 1) {
    const replayStatus = await page.evaluate(async code => (await fetch('/api/login', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ code, remember: false })
    })).status, fixture.code);
    check(`session ${number}: consumed one-time code is rejected`, replayStatus === 401);
  }
  assert.ok(fixture.controlSocket, 'The native fixture must provide its private clock control socket.');
  fixture.code = await new Promise((resolve, reject) => {
    const socket = net.createConnection(fixture.controlSocket);
    const timer = setTimeout(() => { socket.destroy(); reject(new Error('Fixture code control timeout')); }, 10000);
    let data = '';
    socket.on('connect', () => socket.write(JSON.stringify({ action: 'next-code' }) + '\n'));
    socket.on('data', bytes => {
      data += bytes;
      if (!data.includes('\n')) return;
      clearTimeout(timer); socket.destroy();
      try {
        const result = JSON.parse(data.split('\n')[0]);
        assert.ok(result.ok && /^[0-9]{6}$/.test(result.code), 'Fixture did not return a fresh one-time code.');
        resolve(result.code);
      } catch (error) { reject(error); }
    });
    socket.on('error', error => { clearTimeout(timer); reject(error); });
  });
  if (await page.locator('#remember-browser').isVisible()) await page.locator('#remember-browser').uncheck();
  await page.locator('#access-code').fill(fixture.code);
  await page.locator('#connect').click();
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'streaming', null, { timeout: 55000 });
  await page.waitForFunction(() => document.getElementById('desktop-video').videoWidth > 0);
  // Explicit user gesture for receive-only Safari/Chromium audio policy, without changing app code.
  if (await page.locator('#resume-audio').isVisible()) await page.locator('#resume-audio').click();
  if (await page.locator('#sound-toggle').getAttribute('aria-pressed') !== 'true') await page.locator('#sound-toggle').click();
  await new Promise(resolve => setTimeout(resolve, 2000));
  const first = await stats();
  await new Promise(resolve => setTimeout(resolve, 3500));
  const second = await stats();
  sessions.push({ number, first, second });
  const firstVideo = first.inbound.find(report => report.kind === 'video');
  const secondVideo = second.inbound.find(report => report.kind === 'video');
  const firstAudio = first.inbound.find(report => report.kind === 'audio');
  const secondAudio = second.inbound.find(report => report.kind === 'audio');
  check(`session ${number}: WebRTC remains connected`, second.connectionState === 'connected');
  check(`session ${number}: native frame size is the validated 720p baseline`, second.video.width === 1280 && second.video.height === 720);
  check(`session ${number}: H.264 decoder receives continuing frames`, secondVideo?.codec?.mimeType?.toLowerCase() === 'video/h264' && secondVideo.framesDecoded > (firstVideo?.framesDecoded || 0) + 10);
  check(`session ${number}: Opus packets continue arriving`, secondAudio?.codec?.mimeType?.toLowerCase() === 'audio/opus' && secondAudio.packetsReceived > (firstAudio?.packetsReceived || 0));
  check(`session ${number}: fixture sound has nonzero decoded energy`, secondAudio.totalAudioEnergy > 0 && secondAudio.totalAudioEnergy > (firstAudio?.totalAudioEnergy || 0));
  check(`session ${number}: video contains a visible multicolor fixture`, second.pixels.variance > 50 && second.pixels.brightFraction > 0.05 && second.pixels.distinctQuantizedColors > 8);
  check(`session ${number}: rendered fixture pixels change over time`, second.pixels.hash !== first.pixels.hash);
  check(`session ${number}: no credential persisted`, await page.evaluate(() => localStorage.length === 0 && sessionStorage.length === 0 && document.getElementById('access-code').value === ''));
  await page.screenshot({ path: path.join(fixture.outputDir, `native-session-${number}.png`) });
  await page.locator('#desktop-video').screenshot({ path: path.join(fixture.outputDir, `native-video-${number}.png`) });
}

async function disconnect(number) {
  const stopResponse = page.waitForResponse(response => new URL(response.url()).pathname === '/api/session/stop');
  await page.locator('#disconnect').click();
  check(`session ${number}: production stop endpoint succeeds`, (await stopResponse).ok());
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'idle');
  check(`session ${number}: local media closed on disconnect`, await page.evaluate(() => document.getElementById('desktop-video').srcObject === null && window.deskportTestPeers.at(-1).connectionState === 'closed'));
  check(`session ${number}: reconnect requires entering a current one-time code`, await page.locator('#access-code').inputValue() === '');
}

let result;
try {
  const sandbox = await chromiumSandboxStatus(context);
  check('namespace and seccomp sandbox remain enabled', sandbox.namespace && sandbox.seccomp);
  check('no TLS or browser policy bypass flags', sandbox.noBypassFlags);
  await page.goto(fixture.url);
  check('real gateway serves secure context', await page.evaluate(() => isSecureContext));
  await connectAndMeasure(1);
  await disconnect(1);
  await connectAndMeasure(2);
  await disconnect(2);
  check('no uncaught browser errors', errors.length === 0);
  check('both sessions use real successful login requests', apiStatuses.filter(response => response.path === '/api/login' && response.status === 200).length === 2);
  result = { ok: true, browser: browser.version(), checks, sessions, apiStatuses, errors,
    scope: 'Production HTTPS gateway and native capture/encoding/transport with isolated Xvfb/Pulse fixture. No HTTP mocks or TLS/policy bypass. Input is disabled by the fixture gateway. The synthetic root is trusted in a disposable HOME NSS database only. Physical Safari, Windows and OS input are not exercised.' };
  console.log(JSON.stringify({ ok: true, checks: checks.length, output: path.join(fixture.outputDir, 'browser-result.json') }));
} catch (error) {
  let state = {};
  try {
    state = await page.evaluate(() => ({ status: document.getElementById('connection-status')?.textContent, notice: document.getElementById('notice')?.textContent,
      peers: window.deskportTestPeers?.map(peer => ({ connectionState: peer.connectionState, iceConnectionState: peer.iceConnectionState })) }));
    await page.screenshot({ path: path.join(fixture.outputDir, 'native-browser-failure.png') });
  } catch (_) { /* Keep original failure. */ }
  result = { ok: false, error: error.message, browser: browser.version(), checks, sessions, apiStatuses, errors, state };
  console.error(JSON.stringify({ ok: false, error: error.message, state, output: path.join(fixture.outputDir, 'browser-result.json') }));
  process.exitCode = 1;
} finally {
  await writeFile(path.join(fixture.outputDir, 'browser-result.json'), JSON.stringify(result, null, 2));
  await context.close();
  await trusted.close();
}
