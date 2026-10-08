// server.http.test.ts - starts the real server (web/server/app.ts) on a random port with temporary directories and
// checks headers, routing, static files, the data manifest, the endpoints and a two-player WebSocket flow.

import { afterAll, beforeAll, describe, expect, test } from "bun:test";
import { createHash } from "node:crypto";
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { gzipSync } from "node:zlib";
import { PROTOCOL_VERSION, RELAY_FRAME, type RoomState } from "../shared/protocol";
import { startServer, type RunningServer } from "../server/app";
import { loadConfig } from "../server/config";
import { silentLogger } from "../server/log";
import { rawRequest, WsClient } from "./helpers-server";

let tmp: string;
let srv: RunningServer;
let origin: string;
let port: number;

const INDEX = `<!doctype html><html><head><meta charset="utf-8"><title>t</title><script type="module" src="./assets/index-ab12cd34.js"></script></head><body></body></html>`;
const ASSET = `console.log("hello from the client bundle");`.repeat(20);

function write(rel: string, content: string | Uint8Array): void {
  const file = path.join(tmp, rel);
  mkdirSync(path.dirname(file), { recursive: true });
  writeFileSync(file, content);
}

beforeAll(async () => {
  tmp = mkdtempSync(path.join(tmpdir(), "bo1-server-test-"));
  write("public/index.html", INDEX);
  write("public/assets/index-ab12cd34.js", ASSET);
  write("public/assets/index-ab12cd34.js.gz", gzipSync(ASSET));
  write("public/assets/index-ab12cd34.js.br", "BROTLI-PLACEHOLDER");
  write("public/favicon.svg", "<svg xmlns='http://www.w3.org/2000/svg'/>");
  write("public/settings.css", "body{}");
  write("public/big.bin", new Uint8Array(1000).map((_, i) => i & 0xff));
  write("engine/bo1.js", "// engine");
  write("engine/bo1.wasm", new Uint8Array([0, 0x61, 0x73, 0x6d, 1, 0, 0, 0]));
  write("engine/build.json", JSON.stringify({ version: "v1.2.3-test", built: "2026-10-07T00:00:00Z" }));
  write("data-root/mods/zinfo/maps/_zinfo.gsc", "// zinfo\n");
  write("data-root/mods/zinfo/README.md", "# zinfo\n");
  write("data-root/mods/zinfo/.secret", "hidden");
  write("data-root/mods/.git/config", "hidden");
  write("data-root/data/main/playlists_sp.info", "playlists\n");
  write("data-root/data/other/skip.txt", "not published");
  write("data-root/secret.txt", "TOP SECRET");

  const config = loadConfig({
    PORT: "0",
    BASE_PATH: "bo1/",
    PUBLIC_DIR: path.join(tmp, "public"),
    ENGINE_DIR: path.join(tmp, "engine"),
    DATA_ROOT: path.join(tmp, "data-root"),
    TURN_URL: "turn:turn.example:3478",
    TURN_USERNAME: "u",
    TURN_CREDENTIAL: "p",
    LOG_LEVEL: "silent",
  });
  srv = await startServer(config, { logger: silentLogger });
  port = srv.server.port!;
  origin = `http://localhost:${port}`;
});

afterAll(async () => {
  await srv?.stop();
  rmSync(tmp, { recursive: true, force: true });
});

function expectIsolationHeaders(res: Response): void {
  expect(res.headers.get("cross-origin-opener-policy")).toBe("same-origin");
  expect(res.headers.get("cross-origin-embedder-policy")).toBe("require-corp");
  expect(res.headers.get("cross-origin-resource-policy")).toBe("same-origin");
  expect(res.headers.get("x-content-type-options")).toBe("nosniff");
  expect(res.headers.get("referrer-policy")).toBeTruthy();
  const csp = res.headers.get("content-security-policy") ?? "";
  expect(csp).toContain("'wasm-unsafe-eval'");
  expect(csp).toContain("worker-src 'self' blob:");
  expect(csp).toContain("connect-src 'self'");
}

const get = (p: string, init?: RequestInit) => fetch(origin + p, { redirect: "manual", ...init });

describe("routing and headers", () => {
  test("GET / redirects to the base path", async () => {
    const res = await get("/");
    expect(res.status).toBe(302);
    expect(res.headers.get("location")).toBe("/bo1/");
    expectIsolationHeaders(res);
  });

  test("the base path without a slash redirects", async () => {
    const res = await get("/bo1?x=1");
    expect(res.status).toBe(301);
    expect(res.headers.get("location")).toBe("/bo1/?x=1");
  });

  test("index.html: no-cache, base href added, isolation headers", async () => {
    const res = await get("/bo1/");
    expect(res.status).toBe(200);
    expect(res.headers.get("content-type")).toBe("text/html; charset=utf-8");
    expect(res.headers.get("cache-control")).toBe("no-cache");
    expectIsolationHeaders(res);
    const html = await res.text();
    expect(html).toContain('<head>\n<base href="/bo1/">');
    expect(html).toContain("./assets/index-ab12cd34.js");
  });

  test("404s carry the isolation headers too", async () => {
    for (const p of ["/bo1/missing.js", "/elsewhere", "/bo1/api/nothing", "/bo1/engine/nope.wasm", "/bo1/data/mods/nope.gsc"]) {
      const res = await get(p);
      expect(res.status).toBe(404);
      expectIsolationHeaders(res);
    }
  });

  test("client-side routes fall back to index.html", async () => {
    const res = await get("/bo1/room/ABCDE");
    expect(res.status).toBe(200);
    expect(await res.text()).toContain('<base href="/bo1/">');
  });

  test("only GET and HEAD", async () => {
    const res = await get("/bo1/", { method: "POST", body: "x" });
    expect(res.status).toBe(405);
    expectIsolationHeaders(res);
    const head = await get("/bo1/assets/index-ab12cd34.js", { method: "HEAD", headers: { "accept-encoding": "identity" } });
    expect(head.status).toBe(200);
    expect(head.headers.get("content-length")).toBe(String(ASSET.length));
  });
});

describe("static files", () => {
  test("hashed assets are immutable; others get a short cache", async () => {
    const a = await get("/bo1/assets/index-ab12cd34.js", { headers: { "accept-encoding": "identity" } });
    expect(a.headers.get("cache-control")).toBe("public, max-age=31536000, immutable");
    expect(a.headers.get("content-type")).toBe("text/javascript; charset=utf-8");
    expect(await a.text()).toBe(ASSET);
    const b = await get("/bo1/settings.css");
    expect(b.headers.get("cache-control")).toBe("public, max-age=300");
    expect(b.headers.get("content-type")).toBe("text/css; charset=utf-8");
    const svg = await get("/bo1/favicon.svg");
    expect(svg.headers.get("content-type")).toBe("image/svg+xml");
  });

  test("precompressed siblings by Accept-Encoding", async () => {
    const br = await rawRequest(port, "/bo1/assets/index-ab12cd34.js", { "Accept-Encoding": "gzip, br" });
    expect(br.head).toMatch(/content-encoding: br/i);
    expect(br.head).toMatch(/vary: accept-encoding/i);
    expect(br.body).toBe("BROTLI-PLACEHOLDER");
    const gz = await rawRequest(port, "/bo1/assets/index-ab12cd34.js", { "Accept-Encoding": "gzip, br;q=0" });
    expect(gz.head).toMatch(/content-encoding: gzip/i);
    const plain = await rawRequest(port, "/bo1/assets/index-ab12cd34.js", {});
    expect(plain.head).not.toMatch(/content-encoding/i);
    expect(plain.body).toBe(ASSET);
  });

  test("ETag revalidation answers 304", async () => {
    const first = await get("/bo1/settings.css");
    const etag = first.headers.get("etag")!;
    expect(etag).toBeTruthy();
    const again = await get("/bo1/settings.css", { headers: { "if-none-match": etag } });
    expect(again.status).toBe(304);
    expectIsolationHeaders(again);
  });

  test("range requests", async () => {
    const res = await get("/bo1/big.bin", { headers: { range: "bytes=10-19" } });
    expect(res.status).toBe(206);
    expect(res.headers.get("content-range")).toBe("bytes 10-19/1000");
    expect([...new Uint8Array(await res.arrayBuffer())]).toEqual([10, 11, 12, 13, 14, 15, 16, 17, 18, 19]);
    const suffix = await get("/bo1/big.bin", { headers: { range: "bytes=-4" } });
    expect(suffix.headers.get("content-range")).toBe("bytes 996-999/1000");
    const bad = await get("/bo1/big.bin", { headers: { range: "bytes=5000-" } });
    expect(bad.status).toBe(416);
  });

  test("path traversal is rejected", async () => {
    const attempts = [
      "/bo1/..%2f..%2fdata-root%2fsecret.txt",
      "/bo1/%2e%2e/%2e%2e/data-root/secret.txt",
      "/bo1/assets/..%5c..%5cdata-root%5csecret.txt",
      "/bo1/%2fetc%2fpasswd",
      "/bo1/index.html%00.js",
      "/bo1/.%2e/data-root/secret.txt",
      "/bo1/data/..%2fsecret.txt",
      "/bo1/data/mods/zinfo/.secret",
      "/bo1/data/mods/.git/config",
      "/bo1/data/other/skip.txt",
      "/bo1/engine/..%2f..%2fdata-root%2fsecret.txt",
      "/bo1/engine/%2e%2e/data-root/secret.txt",
      "/bo1/../data-root/secret.txt",
      "/bo1/%E0%A4%A.js",
    ];
    for (const p of attempts) {
      const res = await rawRequest(port, p);
      expect(res.body).not.toContain("TOP SECRET");
      expect(res.body).not.toContain("hidden");
      expect(res.body).not.toContain("not published");
      expect([400, 404]).toContain(res.status);
    }
  });
});

describe("data, engine and endpoints", () => {
  test("the manifest lists mods/ and data/main/ (as main/), without dotfiles", async () => {
    const res = await get("/bo1/data/manifest.json");
    expect(res.status).toBe(200);
    expect(res.headers.get("content-type")).toBe("application/json; charset=utf-8");
    expectIsolationHeaders(res);
    const m = (await res.json()) as { files: { path: string; size: number; sha256: string }[] };
    expect(m.files.map((f) => f.path)).toEqual(["main/playlists_sp.info", "mods/zinfo/README.md", "mods/zinfo/maps/_zinfo.gsc"]);
    const gsc = m.files.find((f) => f.path === "mods/zinfo/maps/_zinfo.gsc")!;
    expect(gsc.size).toBe(9);
    expect(gsc.sha256).toBe(createHash("sha256").update("// zinfo\n").digest("hex"));
  });

  test("data files are served by their manifest path", async () => {
    const res = await get("/bo1/data/mods/zinfo/maps/_zinfo.gsc");
    expect(res.status).toBe(200);
    expect(res.headers.get("content-type")).toBe("text/plain; charset=utf-8");
    expect(await res.text()).toBe("// zinfo\n");
    const info = await get("/bo1/data/main/playlists_sp.info");
    expect(await info.text()).toBe("playlists\n");
  });

  test("engine files", async () => {
    const wasm = await get("/bo1/engine/bo1.wasm");
    expect(wasm.status).toBe(200);
    expect(wasm.headers.get("content-type")).toBe("application/wasm");
    expect(wasm.headers.get("cache-control")).toBe("no-cache");
    expectIsolationHeaders(wasm);
    const js = await get("/bo1/engine/bo1.js");
    expect(js.headers.get("content-type")).toBe("text/javascript; charset=utf-8");
  });

  test("healthz", async () => {
    const res = await get("/bo1/healthz");
    expect(res.status).toBe(200);
    expectIsolationHeaders(res);
    const body = (await res.json()) as Record<string, unknown>;
    expect(body).toMatchObject({ ok: true, rooms: 0, engine: true });
    expect(typeof body.peers).toBe("number");
    expect(typeof body.uptime).toBe("number");
  });

  test("api/config", async () => {
    const res = await get("/bo1/api/config");
    expect(await res.json()).toEqual({
      iceServers: [
        { urls: ["stun:stun.l.google.com:19302", "stun:stun1.l.google.com:19302"] },
        { urls: ["turn:turn.example:3478"], username: "u", credential: "p" },
      ],
      maxPlayers: 8,
      engine: { available: true, version: "v1.2.3-test" },
      protocolVersion: PROTOCOL_VERSION,
    });
  });
});

describe("WebSocket", () => {
  test("a cross-origin upgrade is refused", async () => {
    const res = await rawRequest(port, "/bo1/ws", {
      Origin: "https://evil.example",
      Upgrade: "websocket",
      Connection: "Upgrade",
      "Sec-WebSocket-Key": "dGhlIHNhbXBsZSBub25jZQ==",
      "Sec-WebSocket-Version": "13",
    });
    expect(res.status).toBe(403);
  });

  test("two players: create, join, pick, ready, files, start, relay", async () => {
    const wsUrl = `ws://localhost:${port}/bo1/ws`;
    const host = await WsClient.open(wsUrl, { Origin: origin });
    const guest = await WsClient.open(wsUrl);
    try {
      host.send({ t: "hello", version: PROTOCOL_VERSION, name: "Host" });
      guest.send({ t: "hello", version: PROTOCOL_VERSION, name: "Guest" });
      const hw = await host.waitForType("welcome");
      const gw = await guest.waitForType("welcome");
      expect(hw.iceServers).toHaveLength(2);

      host.send({ t: "room.create", settings: { map: "zombie_pentagon", maxPlayers: 2 } });
      const created = await host.waitForType("room.state");
      const code = created.room.code;

      guest.send({ t: "room.join", code });
      await guest.waitForType("room.state", (m) => m.room.code === code);
      guest.send({ t: "slot.character", character: 2 });
      await guest.waitForType("room.state", (m) => m.room.slots[1]!.character === 2);

      for (const c of [host, guest]) {
        c.send({ t: "slot.files", hasGameFiles: true });
        c.send({ t: "slot.ready", ready: true });
      }
      const allReady = (r: RoomState) => r.slots.every((s) => s.ready && s.hasGameFiles);
      await host.waitForType("room.state", (m) => allReady(m.room));

      host.send({ t: "room.start" });
      const hs = await host.waitForType("room.started");
      const gs = await guest.waitForType("room.started");
      expect(hs.launch.yourSlot).toBe(0);
      expect(gs.launch.yourSlot).toBe(1);
      expect(gs.launch.hostSlot).toBe(0);
      expect(gs.launch.players).toEqual([
        { slot: 0, clientNum: 0, name: "Host", character: 0 },
        { slot: 1, clientNum: 2, name: "Guest", character: 2 },
      ]);

      // signaling goes to the other peer
      guest.send({ t: "rtc.signal", to: hw.peerId, signal: { type: "offer", sdp: "v=0\r\n" } });
      const sig = await host.waitForType("rtc.signal");
      expect(sig.from).toBe(gw.peerId);

      // relay: guest -> slot 0 arrives at the host marked as from slot 1
      guest.sendBinary(new Uint8Array([RELAY_FRAME, 0, 0xde, 0xad, 0xbe, 0xef]));
      const frame = await host.waitFor<Uint8Array>((m) => m instanceof Uint8Array);
      expect([...frame]).toEqual([RELAY_FRAME, 1, 0xde, 0xad, 0xbe, 0xef]);
      host.sendBinary(new Uint8Array([RELAY_FRAME, 1, 7]));
      const back = await guest.waitFor<Uint8Array>((m) => m instanceof Uint8Array);
      expect([...back]).toEqual([RELAY_FRAME, 0, 7]);

      const health = (await (await get("/bo1/healthz")).json()) as { rooms: number; peers: number };
      expect(health).toMatchObject({ rooms: 1, peers: 2 });

      // the host leaving in-game closes the room for the guest
      host.send({ t: "room.leave" });
      const left = await guest.waitForType("room.left");
      expect(left.reason).toBe("closed");
    } finally {
      host.close();
      guest.close();
    }
  });

  test("stop() closes sockets with 1012", async () => {
    const config = loadConfig({ PORT: "0", BASE_PATH: "", PUBLIC_DIR: path.join(tmp, "nope"), DATA_ROOT: path.join(tmp, "data-root"), ENGINE_DIR: path.join(tmp, "nope") });
    const s2 = await startServer(config, { logger: silentLogger });
    const res = await fetch(`http://localhost:${s2.server.port}/`);
    expect(res.status).toBe(200);
    expect(await res.text()).toContain("the browser client is not built");
    const cfg = (await (await fetch(`http://localhost:${s2.server.port}/api/config`)).json()) as { engine: unknown };
    expect(cfg.engine).toEqual({ available: false });
    const c = await WsClient.open(`ws://localhost:${s2.server.port}/ws`);
    c.send({ t: "ping", time: 5 });
    expect((await c.waitForType("pong")).time).toBe(5);
    await s2.stop();
    for (let i = 0; i < 50 && !c.closed; i++) await Bun.sleep(10);
    expect(c.closed?.code).toBe(1012);
  });
});
