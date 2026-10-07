// Shared headless-Chromium launcher for browser QA scripts.
// @sparticuz/chromium ships a Chromium build plus SwiftShader (software GPU) and
// the NSS libraries it needs; the latter are only auto-extracted on AWS Lambda,
// so we unpack them here for ordinary Linux hosts.
import chromium from '@sparticuz/chromium';
import puppeteer from 'puppeteer-core';
import fs from 'node:fs';
import zlib from 'node:zlib';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const LIB_DIR = '/tmp/al2023/lib';

function ensureSystemLibs() {
  if (fs.existsSync(path.join(LIB_DIR, 'libnss3.so'))) return;
  const br = path.join(here, 'node_modules/@sparticuz/chromium/bin/al2023.tar.br');
  fs.mkdirSync('/tmp/al2023', { recursive: true });
  fs.writeFileSync('/tmp/al2023.tar', zlib.brotliDecompressSync(fs.readFileSync(br)));
  execFileSync('tar', ['-xf', '/tmp/al2023.tar', '-C', '/tmp/al2023']);
}

export async function launchBrowser({ width = 1280, height = 720 } = {}) {
  const executablePath = await chromium.executablePath();
  ensureSystemLibs();
  return puppeteer.launch({
    executablePath,
    headless: 'shell',
    protocolTimeout: 900000,  // software GL frames can take many seconds
    defaultViewport: { width, height, deviceScaleFactor: 1 },
    env: { ...process.env, LD_LIBRARY_PATH: `${LIB_DIR}:/tmp:${process.env.LD_LIBRARY_PATH ?? ''}` },
    args: [
      ...chromium.args.filter(a => !a.startsWith('--window-size') && a !== '--single-process'),
      '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist',
      `--window-size=${width},${height}`,
    ],
  });
}
