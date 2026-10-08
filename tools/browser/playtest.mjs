// Automated browser playtest of the WebAssembly build.
//
//   node playtest.mjs --dir ../../build/web-dev/src/app --out ../../qa/run [--query "hours=7.5"] [--script default]
//
// Serves the build over HTTP, loads it in headless Chromium (SwiftShader WebGL2 —
// software rendering, so timings are NOT representative of real GPUs), waits for the
// game's ready signal, drives scripted keyboard/mouse input and saves screenshots plus
// a console log and a JSON summary.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { launchBrowser } from './launch.mjs';

const args = Object.fromEntries(process.argv.slice(2).reduce((acc, a, i, all) => {
  if (a.startsWith('--')) acc.push([a.slice(2), all[i + 1] && !all[i + 1].startsWith('--') ? all[i + 1] : 'true']);
  return acc;
}, []));
const dir = path.resolve(args.dir ?? '../../build/web-dev/src/app');
const out = path.resolve(args.out ?? '../../qa/latest');
// The `default` script toggles the diagnostics overlay with F3; the overlay only exists
// with ?debug=1, so make sure the flag is present for that script (README: URL options).
let query = args.query ?? '';
const script = args.script ?? 'default';
if (script === 'default' && !/[?&]debug=1(&|$)/.test(query)) query = (query ? query + '&' : '') + 'debug=1';
const width = Number(args.width ?? 1280), height = Number(args.height ?? 720);
const readyTimeout = Number(args.timeout ?? 600) * 1000;
fs.mkdirSync(out, { recursive: true });

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.data': 'application/octet-stream',
  '.map': 'application/json', '.json': 'application/json' };
let server = null;
let pageUrl;
if (fileMode) {
  // file:// mode: the single-file build must work with no HTTP server at all.
  pageUrl = 'file://' + fileMode + (query ? (fileMode.includes('?') ? '&' : '?') + query : '');
} else {
  server = http.createServer((req, res) => {
    const url = decodeURIComponent(req.url.split('?')[0]);
    const file = path.join(dir, url === '/' ? 'index.html' : url);
    if (!file.startsWith(dir) || !fs.existsSync(file)) { res.writeHead(404); res.end(); return; }
    res.writeHead(200, { 'Content-Type': MIME[path.extname(file)] ?? 'application/octet-stream' });
    fs.createReadStream(file).pipe(res);
  });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const port = server.address().port;
  pageUrl = `http://127.0.0.1:${port}/?${query}`;
}

const log = [];
const t0 = Date.now();
const stamp = () => ((Date.now() - t0) / 1000).toFixed(1).padStart(6);
const browser = await launchBrowser({ width, height });
const page = await browser.newPage();
page.on('console', m => log.push(`${stamp()} [${m.type()}] ${m.text()}`));
page.on('pageerror', e => log.push(`${stamp()} [pageerror] ${e.message}`));
page.on('requestfailed', r => log.push(`${stamp()} [requestfailed] ${r.url()} ${r.failure()?.errorText}`));

const summary = { url: pageUrl, fileMode: !!fileMode, viewport: [width, height], shots: [], renderer: null };
const shot = async (name) => {
  const file = path.join(out, `${name}.png`);
  await page.screenshot({ path: file });
  summary.shots.push(file);
  log.push(`${stamp()} [qa] screenshot ${name}`);
};
const sleep = ms => new Promise(r => setTimeout(r, ms));
const hold = async (keys, ms) => { for (const k of keys) await page.keyboard.down(k); await sleep(ms); for (const k of keys.reverse()) await page.keyboard.up(k); };
const press = async (k) => { await page.keyboard.down(k); await sleep(80); await page.keyboard.up(k); };
// Mouse look: SDL reads movementX/Y from mousemove events on the canvas.
const look = async (dx, dy, steps = 20) => {
  await page.evaluate(async (dx, dy, steps) => {
    const c = document.getElementById('canvas');
    for (let i = 0; i < steps; i++) {
      c.dispatchEvent(new MouseEvent('mousemove', { bubbles: true, movementX: dx / steps, movementY: dy / steps, clientX: 640, clientY: 360 }));
      await new Promise(r => requestAnimationFrame(r));
    }
  }, dx, dy, steps);
};
const measureFps = async (ms) => page.evaluate(ms => new Promise(res => {
  let n = 0; const s = performance.now();
  const f = () => { n++; if (performance.now() - s < ms) requestAnimationFrame(f); else res(n * 1000 / (performance.now() - s)); };
  requestAnimationFrame(f);
}), ms);

let ok = false;
try {
  await page.goto(summary.url, { waitUntil: 'load', timeout: 120000 });
  summary.webgl = await page.evaluate(() => {
    const gl = document.createElement('canvas').getContext('webgl2');
    const ext = gl && gl.getExtension('WEBGL_debug_renderer_info');
    return gl ? { renderer: ext ? gl.getParameter(ext.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER), version: gl.getParameter(gl.VERSION) } : null;
  });
  await shot('00-loading');
  await page.waitForFunction(() => window.__mistpineReady === true || document.getElementById('error').style.display === 'flex',
                             { timeout: readyTimeout, polling: 500 });
  const error = await page.evaluate(() => document.getElementById('error').style.display === 'flex' ? document.getElementById('errortext').textContent : null);
  if (error) throw new Error('game reported error: ' + error);
  summary.readySeconds = (Date.now() - t0) / 1000;
  await sleep(2500);  // let the overlay fade and streaming settle
  await shot('00-title');
  summary.titleMenu = await page.evaluate(() => window.__mistpineState && window.__mistpineState.menu);
  await page.click('#canvas').catch(() => {});
  await sleep(1500);  // menu veil fades, HUD fades in
  if (script === 'default') {
    summary.playingMenu = await page.evaluate(() => window.__mistpineState && window.__mistpineState.menu);
    if (summary.playingMenu !== 'playing') throw new Error('expected playing after the title click, got: ' + summary.playingMenu);
    await shot('01-spawn');
    summary.rafFpsIdle = await measureFps(3000);
    await look(-260, 0);
    await sleep(800);
    await shot('02-look-left');
    await hold(['ShiftLeft', 'KeyW'], 4000);
    await sleep(600);
    await shot('03-after-run');
    await look(500, 40);
    await sleep(800);
    await shot('04-look-right');
    await press('F3');
    await sleep(1500);
    await shot('05-debug-overlay');
    await press('F3');
  } else if (script === 'persist') {
    // Save round trip through real browser localStorage: play, pause (saves), reload, continue.
    await hold(['KeyW'], 3000);
    await press('Escape');
    await sleep(2500);
    await shot('01-paused');
    summary.pausedMenu = await page.evaluate(() => window.__mistpineState && window.__mistpineState.menu);
    const save = await page.evaluate(() => localStorage.getItem('mistpine-save'));
    summary.saveBytes = save ? save.length : 0;
    summary.saveHead = save ? save.slice(0, 120) : null;
    if (!save) throw new Error('no save in localStorage after pausing');
    if (summary.pausedMenu !== 'pause') throw new Error('expected the pause menu, got: ' + summary.pausedMenu);
    await page.goto(summary.url.replace(/[?&]new=1/, (m) => m[0] === '?' ? '?' : ''), { waitUntil: 'load' });  // reload without ?new=1
    await page.waitForFunction(() => window.__mistpineReady === true, { timeout: readyTimeout, polling: 500 });
    await sleep(2500);
    // The in-engine title screen reports itself through window.__mistpineState.
    const titleState = await page.evaluate(() => window.__mistpineState);
    summary.titleState = titleState ? { menu: titleState.menu, hasSave: titleState.hasSave, version: titleState.version } : null;
    await shot('02-continue-title');
    if (!titleState || titleState.menu !== 'title' || !titleState.hasSave)
      throw new Error('title state does not offer Continue: ' + JSON.stringify(summary.titleState));
    await page.click('#canvas');
    await sleep(2500);
    summary.resumedMenu = await page.evaluate(() => window.__mistpineState && window.__mistpineState.menu);
    if (summary.resumedMenu !== 'playing') throw new Error('expected playing after continue, got: ' + summary.resumedMenu);
    await shot('03-resumed');
  } else if (script === 'still') {
    await sleep(1500);
    await shot('01-still');
    summary.rafFpsIdle = await measureFps(3000);
  }
  ok = true;
} catch (e) {
  log.push(`${stamp()} [qa] FAILED: ${e.message}`);
  await shot('99-failure').catch(() => {});
} finally {
  summary.ok = ok;
  summary.consoleErrors = log.filter(l => /\[(error|pageerror)\]/.test(l)).length;
  // Release gate (PROMPT §9.9): zero browser console errors are required.
  if (summary.consoleErrors > 0) {
    ok = false;
    summary.ok = false;
    log.push(`${stamp()} [qa] FAILED: ${summary.consoleErrors} console error(s), see console.log`);
  }
  fs.writeFileSync(path.join(out, 'console.log'), log.join('\n') + '\n');
  fs.writeFileSync(path.join(out, 'summary.json'), JSON.stringify(summary, null, 2));
  await browser.close();
  server.close();
  console.log(JSON.stringify(summary, null, 2));
  process.exit(ok ? 0 : 1);
}
