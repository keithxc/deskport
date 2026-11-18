// Test-only Linux Chromium trust. No system profile, security flags or OS input.
import assert from 'node:assert/strict';
import { mkdir, mkdtemp, rm } from 'node:fs/promises';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

export async function launchTrustedChromium({ chromium, executablePath, certutil, certificate, output }) {
  assert.equal(process.platform, 'linux', 'Private NSS fixture trust requires Linux.');
  assert.ok(certutil && certificate, 'Supply certutil and the exact synthetic fixture certificate.');
  await mkdir(output, { recursive: true });
  const trustHome = await mkdtemp(path.join(output, 'trust-home-'));
  const runtimeRoot = path.join(process.env.DESKPORT_BUILD_ROOT || path.join(process.env.HOME, 'mygit/build/deskport'), 't');
  await mkdir(runtimeRoot, { recursive: true });
  const temporary = await mkdtemp(path.join(runtimeRoot, 'c-'));
  const previous = Object.fromEntries(['TMPDIR', 'TMP', 'TEMP'].map(name => [name, process.env[name]]));
  let browser;
  try {
    for (const relative of ['.pki/nssdb', '.local/share/pki/nssdb']) {
      const directory = path.join(trustHome, relative), database = `sql:${directory}`;
      await mkdir(directory, { recursive: true });
      execFileSync(certutil, ['-N', '--empty-password', '-d', database]);
      execFileSync(certutil, ['-A', '-d', database, '-n', 'DeskPort isolated fixture', '-t', 'C,,', '-i', certificate]);
    }
    for (const name of Object.keys(previous)) process.env[name] = temporary;
    browser = await chromium.launch({ executablePath, headless: true, chromiumSandbox: true, ignoreDefaultArgs: true,
      args: ['--headless=new', '--remote-debugging-pipe', '--no-first-run', '--no-default-browser-check',
        '--mute-audio', '--user-data-dir=' + path.join(temporary, 'profile')],
      env: { ...process.env, HOME: trustHome, XDG_DATA_HOME: path.join(trustHome, '.local/share') } });
  } catch (error) {
    await rm(trustHome, { recursive: true, force: true });
    await rm(temporary, { recursive: true, force: true });
    throw error;
  } finally {
    for (const [name, value] of Object.entries(previous)) {
      if (value === undefined) delete process.env[name]; else process.env[name] = value;
    }
  }
  return { browser, close: async () => {
    await browser.close();
    await rm(trustHome, { recursive: true, force: true });
    await rm(temporary, { recursive: true, force: true });
  } };
}

export async function chromiumSandboxStatus(context) {
  const page = await context.newPage();
  try {
    await page.goto('chrome://sandbox');
    const text = await page.locator('body').innerText();
    const namespace = /Namespace sandbox[\s:]*Yes/i.test(text) ||
      (/PID namespaces[\s:]*Yes/i.test(text) && /Network namespaces[\s:]*Yes/i.test(text));
    const seccomp = /Seccomp-BPF sandbox[\s:]*Yes/i.test(text);
    await page.goto('chrome://version');
    const flags = await page.locator('#command_line').textContent();
    const noBypassFlags = !/--(?:no-sandbox|disable-setuid-sandbox|ignore-certificate-errors|disable-web-security|autoplay-policy)(?:\s|=|$)/.test(flags);
    return { namespace, seccomp, noBypassFlags, text };
  } finally { await page.close(); }
}
