// run_smoke_cdp.mjs - open a smoke test page in headless Chromium via the DevTools protocol, wait (real time)
// until window.D3D9_SMOKE.done, print the page log, exit 0 on PASS.
//
//   node src/web/d3d9/test/run_smoke_cdp.mjs <chrome> <url> [angle-backend] [timeout-seconds]
//
// Node >= 22 (global WebSocket). Used by run_smoke_headless.sh.
import { spawn } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const [chrome, url, angle = 'swiftshader', timeoutSec = '90'] = process.argv.slice(2);
if (!chrome || !url) {
    console.error('usage: run_smoke_cdp.mjs <chrome> <url> [angle] [timeout-seconds]');
    process.exit(2);
}
const profile = mkdtempSync(join(tmpdir(), 'd3d9smoke-'));
const proc = spawn(chrome, [
    `--user-data-dir=${profile}`, '--headless=new', '--no-first-run', '--remote-debugging-port=0',
    `--use-angle=${angle}`, '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist', 'about:blank',
], { stdio: ['ignore', 'ignore', 'pipe'] });

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let stderr = '';
const port = await new Promise((resolve, reject) => {
    const t = setTimeout(() => reject(new Error('chrome did not report a DevTools port')), 20000);
    proc.stderr.on('data', (d) => {
        stderr += d;
        const m = /DevTools listening on ws:\/\/[^:]+:(\d+)\//.exec(stderr);
        if (m) { clearTimeout(t); resolve(m[1]); }
    });
});

async function finish(code) {
    proc.kill();
    await sleep(200);
    try { rmSync(profile, { recursive: true, force: true }); } catch {}
    process.exit(code);
}

try {
    const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
    const page = targets.find((t) => t.type === 'page');
    const ws = new WebSocket(page.webSocketDebuggerUrl);
    await new Promise((r) => ws.addEventListener('open', r, { once: true }));
    let id = 0;
    const pending = new Map();
    ws.addEventListener('message', (ev) => {
        const msg = JSON.parse(ev.data);
        if (msg.id && pending.has(msg.id)) { pending.get(msg.id)(msg); pending.delete(msg.id); }
    });
    const send = (method, params = {}) => new Promise((r) => { const i = ++id; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
    const evaluate = async (expression) => (await send('Runtime.evaluate', { expression, returnByValue: true })).result?.result?.value;
    await send('Page.enable');
    await send('Page.navigate', { url });
    const deadline = Date.now() + Number(timeoutSec) * 1000;
    let state = null;
    while (Date.now() < deadline) {
        await sleep(500);
        state = await evaluate('window.D3D9_SMOKE || null');
        if (state && state.done) break;
    }
    const log = await evaluate("document.getElementById('log') ? document.getElementById('log').innerText : ''");
    process.stdout.write(log || '(no log)\n');
    if (!state || !state.done) {
        console.log(`result: TIMEOUT after ${timeoutSec}s (title: ${await evaluate('document.title')})`);
        await finish(1);
    }
    console.log(`result (${angle}): ${state.fail ? 'FAIL' : 'PASS'} ${state.pass}/${state.pass + state.fail}`);
    await finish(state.fail ? 1 : 0);
} catch (e) {
    console.error('error:', e.message);
    await finish(2);
}
