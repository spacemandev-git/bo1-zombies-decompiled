// app.ts - the HTTP server: static client, engine files, data manifest, health/config endpoints and the lobby
// WebSocket, all under config.basePath. index.ts runs it from environment variables; tests start it directly.

import { brotliCompressSync, constants as zlib } from "node:zlib";
import { readFile } from "node:fs/promises";
import path from "node:path";
import type { Server, ServerWebSocket } from "bun";
import { PROTOCOL_VERSION } from "../shared/protocol";
import { MAX_PLAYERS_EXTENDED } from "../shared/roster";
import type { ServerConfig } from "./config";
import { Lobby, type ConnId, type LobbyLimits } from "./lobby";
import type { Logger } from "./log";
import { buildManifest, type DataManifest } from "./manifest";
import {
  CACHE_IMMUTABLE,
  CACHE_NONE,
  CACHE_REVALIDATE,
  CACHE_SHORT,
  fileInfo,
  isHashedAsset,
  resolveInside,
  safeSegments,
  serveFile,
  serveMemory,
  type MemoryAsset,
} from "./static";

interface WsData {
  conn: ConnId;
}

export interface RunningServer {
  server: Server<WsData>;
  url: string;
  lobby: Lobby;
  /** Closes every WebSocket (1012) and stops the server. */
  stop(): Promise<void>;
}

export interface StartOptions {
  logger: Logger;
  /** Lobby limit overrides (tests). */
  limits?: Partial<LobbyLimits>;
  /** How often the lobby expires dropped peers and idle rooms, ms. */
  tickMs?: number;
}

/** Relay frames to a socket with more than this queued are dropped (UDP semantics) instead of buffered. */
const RELAY_BACKPRESSURE_BYTES = 256 * 1024;
const CLOSE_RESTART = 1012;

/** Headers on every response (COOP/COEP make the page crossOriginIsolated, which SharedArrayBuffer needs). */
export function securityHeaders(req: Request): Record<string, string> {
  const connect = ["'self'"];
  const host = req.headers.get("host");
  // older Safari does not treat ws(s) to the page's own host as 'self'
  if (host && /^[A-Za-z0-9.-]+(:\d{1,5})?$/.test(host)) connect.push(`wss://${host}`, `ws://${host}`);
  const csp = [
    "default-src 'self'",
    "script-src 'self' 'wasm-unsafe-eval' blob:",
    "worker-src 'self' blob:",
    `connect-src ${connect.join(" ")}`,
    "img-src 'self' data: blob:",
    "media-src 'self' blob:",
    "style-src 'self' 'unsafe-inline'",
    "font-src 'self' data:",
    "object-src 'none'",
    "base-uri 'self'",
    "form-action 'self'",
    "frame-ancestors 'none'",
  ].join("; ");
  return {
    "Cross-Origin-Opener-Policy": "same-origin",
    "Cross-Origin-Embedder-Policy": "require-corp",
    "Cross-Origin-Resource-Policy": "same-origin",
    "X-Content-Type-Options": "nosniff",
    "Referrer-Policy": "strict-origin-when-cross-origin",
    "Content-Security-Policy": csp,
  };
}

function json(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "Content-Type": "application/json; charset=utf-8", "Cache-Control": CACHE_NONE },
  });
}

function text(body: string, status: number, extra: Record<string, string> = {}): Response {
  return new Response(body, { status, headers: { "Content-Type": "text/plain; charset=utf-8", "Cache-Control": CACHE_NONE, ...extra } });
}

function redirect(location: string, status: 301 | 302): Response {
  return new Response(null, { status, headers: { Location: location, "Cache-Control": CACHE_NONE } });
}

function escapeHtml(s: string): string {
  return s.replace(/[&<>"']/g, (c) => `&#${c.charCodeAt(0)};`);
}

function fallbackPage(basePath: string): string {
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<base href="${escapeHtml(basePath)}/">
<title>BO1 Zombies</title>
</head>
<body>
<h1>BO1 Zombies</h1>
<p>The server is running, but the browser client is not built. Build it with:</p>
<pre>cd web
bun install
bun run build</pre>
<p>Health: <a href="healthz">healthz</a>. Configuration: <a href="api/config">api/config</a>.</p>
</body>
</html>
`;
}

function memoryAsset(type: string, body: string): MemoryAsset {
  const identity = new TextEncoder().encode(body);
  const hash = new Bun.CryptoHasher("sha256").update(identity).digest("hex").slice(0, 16);
  return {
    type,
    etag: `"${hash}"`,
    identity,
    gzip: Bun.gzipSync(identity, { level: 9 }),
    br: new Uint8Array(brotliCompressSync(identity, { params: { [zlib.BROTLI_PARAM_QUALITY]: 11 } })),
  };
}

/** index.html with <base href="BASE_PATH/"> added when it has none, so relative asset URLs work on deep links. */
export function withBaseHref(html: string, basePath: string): string {
  if (/<base\s/i.test(html)) return html;
  const tag = `<base href="${escapeHtml(basePath)}/">`;
  const m = /<head[^>]*>/i.exec(html);
  if (!m) return tag + html;
  const at = m.index + m[0].length;
  return html.slice(0, at) + "\n" + tag + html.slice(at);
}

export async function startServer(config: ServerConfig, opts: StartOptions): Promise<RunningServer> {
  const { logger } = opts;
  const startedAt = Date.now();
  const base = config.basePath;
  const sockets = new Map<ConnId, ServerWebSocket<WsData>>();
  let nextConn = 1;

  const lobby = new Lobby({
    now: () => Date.now(),
    randomBytes: (n) => crypto.getRandomValues(new Uint8Array(n)),
    iceServers: config.iceServers,
    limits: { maxRooms: config.maxRooms, ...opts.limits },
    send: (id, msg) => {
      const ws = sockets.get(id);
      if (!ws) return;
      if (msg instanceof Uint8Array) {
        if (ws.getBufferedAmount() <= RELAY_BACKPRESSURE_BYTES) ws.sendBinary(msg);
      } else {
        ws.sendText(JSON.stringify(msg));
      }
    },
    // deferred: closing from inside a lobby call must not re-enter the lobby
    disconnect: (id, code, reason) => queueMicrotask(() => sockets.get(id)?.close(code, reason)),
    log: (level, event, fields) => logger[level](event, fields),
  });
  const tick = setInterval(() => lobby.tick(), opts.tickMs ?? 1000);

  const manifest: DataManifest = await buildManifest(config.dataRoot);
  const manifestAsset = memoryAsset("application/json; charset=utf-8", manifest.json);

  // index.html is served from memory (with the base tag added); reloaded when the file changes
  let indexCache: { mtimeMs: number; size: number; asset: MemoryAsset } | null = null;
  const fallbackAsset = memoryAsset("text/html; charset=utf-8", fallbackPage(base));
  async function indexAsset(): Promise<MemoryAsset> {
    const file = path.join(config.publicDir, "index.html");
    const info = await fileInfo(file);
    if (!info) return fallbackAsset;
    if (!indexCache || indexCache.mtimeMs !== info.mtimeMs || indexCache.size !== info.size) {
      const html = withBaseHref(await readFile(file, "utf8"), base);
      indexCache = { ...info, asset: memoryAsset("text/html; charset=utf-8", html) };
    }
    return indexCache.asset;
  }

  // engine availability, re-checked at most every 5 s (the engine can be built while the server runs)
  let engineCache: { at: number; available: boolean; version?: string } = { at: -Infinity, available: false };
  async function engineInfo(): Promise<{ available: boolean; version?: string }> {
    if (Date.now() - engineCache.at < 5000) return engineCache;
    const [js, wasm] = await Promise.all([
      fileInfo(path.join(config.engineDir, "bo1.js")),
      fileInfo(path.join(config.engineDir, "bo1.wasm")),
    ]);
    const next: typeof engineCache = { at: Date.now(), available: js !== null && wasm !== null };
    try {
      const build = JSON.parse(await readFile(path.join(config.engineDir, "build.json"), "utf8")) as { version?: unknown };
      if (typeof build.version === "string") next.version = build.version;
    } catch {
      // no build.json: no version
    }
    engineCache = next;
    return next;
  }

  async function serveStatic(req: Request, rel: string): Promise<Response> {
    if (rel === "/" || rel === "/index.html") return serveMemory(req, await indexAsset(), CACHE_REVALIDATE);
    const segs = safeSegments(rel);
    if (!segs) return text("Bad Request\n", 400);
    const file = resolveInside(config.publicDir, segs);
    if (!file) return text("Bad Request\n", 400);
    const res = await serveFile(req, file, { cacheControl: isHashedAsset(file) ? CACHE_IMMUTABLE : CACHE_SHORT });
    if (res) return res;
    // client-side routes (no file extension) get the app; missing files stay 404
    if (!segs[segs.length - 1]!.includes(".")) return serveMemory(req, await indexAsset(), CACHE_REVALIDATE);
    return text("Not Found\n", 404);
  }

  async function route(req: Request, server: Server<WsData>): Promise<Response | undefined> {
    const url = new URL(req.url);
    const pathname = url.pathname;
    let rel: string;
    if (base === "") {
      rel = pathname;
    } else {
      if (pathname === "/" && config.rootRedirect) return redirect(`${base}/`, 302);
      if (pathname === base) return redirect(`${base}/${url.search}`, 301);
      if (!pathname.startsWith(`${base}/`)) return text("Not Found\n", 404);
      rel = pathname.slice(base.length);
    }
    if (req.method !== "GET" && req.method !== "HEAD") return text("Method Not Allowed\n", 405, { Allow: "GET, HEAD" });

    if (rel === "/ws") {
      const origin = req.headers.get("origin");
      if (origin !== null) {
        let originHost = "";
        try {
          originHost = new URL(origin).host;
        } catch {
          // "null" or garbage
        }
        if (originHost !== req.headers.get("host")) return text("Forbidden\n", 403);
      }
      const conn = nextConn++;
      if (server.upgrade(req, { data: { conn } })) return undefined;
      return text("Upgrade Required\n", 426, { Upgrade: "websocket" });
    }
    if (rel === "/healthz") {
      const s = lobby.stats();
      const engine = await engineInfo();
      return json({ ok: true, rooms: s.rooms, peers: s.peers, uptime: Math.round((Date.now() - startedAt) / 1000), engine: engine.available });
    }
    if (rel === "/api/config") {
      const engine = await engineInfo();
      return json({
        iceServers: config.iceServers,
        maxPlayers: MAX_PLAYERS_EXTENDED,
        engine: engine.version !== undefined ? { available: engine.available, version: engine.version } : { available: engine.available },
        protocolVersion: PROTOCOL_VERSION,
      });
    }
    if (rel.startsWith("/api/")) return json({ error: "not found" }, 404);
    if (rel === "/data/manifest.json") return serveMemory(req, manifestAsset, CACHE_REVALIDATE);
    if (rel.startsWith("/data/")) {
      const segs = safeSegments(rel.slice("/data/".length));
      if (!segs) return text("Bad Request\n", 400);
      const file = manifest.byPath.get(segs.join("/"));
      const res = file ? await serveFile(req, file, { cacheControl: CACHE_REVALIDATE }) : null;
      return res ?? text("Not Found\n", 404);
    }
    if (rel === "/engine" || rel.startsWith("/engine/")) {
      const segs = safeSegments(rel.slice("/engine".length));
      if (!segs) return text("Bad Request\n", 400);
      const file = segs.length ? resolveInside(config.engineDir, segs) : null;
      // bo1.js / bo1.wasm keep their names across builds: always revalidate (cheap 304s via ETag)
      const res = file ? await serveFile(req, file, { cacheControl: CACHE_REVALIDATE }) : null;
      return res ?? text("Not Found\n", 404);
    }
    return serveStatic(req, rel);
  }

  const server = Bun.serve<WsData>({
    port: config.port,
    async fetch(req, server) {
      let res: Response | undefined;
      try {
        res = await route(req, server);
      } catch (e) {
        logger.error("http.error", { method: req.method, url: req.url, error: e as Error });
        res = text("Internal Server Error\n", 500);
      }
      if (res === undefined) return undefined; // upgraded to a WebSocket
      for (const [k, v] of Object.entries(securityHeaders(req))) res.headers.set(k, v);
      return res;
    },
    error(e) {
      logger.error("http.error", { error: e });
      return new Response("Internal Server Error\n", { status: 500 });
    },
    websocket: {
      maxPayloadLength: 64 * 1024,
      idleTimeout: 60,
      sendPings: true,
      perMessageDeflate: false,
      backpressureLimit: 4 * 1024 * 1024,
      closeOnBackpressureLimit: false,
      open(ws) {
        sockets.set(ws.data.conn, ws);
        lobby.open(ws.data.conn);
      },
      message(ws, data) {
        try {
          lobby.message(ws.data.conn, data);
        } catch (e) {
          logger.error("ws.error", { conn: ws.data.conn, error: e as Error });
        }
      },
      close(ws) {
        sockets.delete(ws.data.conn);
        lobby.close(ws.data.conn);
      },
    },
  });

  const url = `http://localhost:${server.port}${base}/`;
  logger.info("server.start", {
    port: server.port,
    basePath: base,
    publicDir: config.publicDir,
    engineDir: config.engineDir,
    dataFiles: manifest.files.length,
    maxRooms: config.maxRooms,
  });

  return {
    server,
    url,
    lobby,
    async stop() {
      clearInterval(tick);
      const open = sockets.size;
      for (const ws of sockets.values()) ws.close(CLOSE_RESTART, "server restarting");
      if (open) await Bun.sleep(100); // let the close frames go out before the connections are cut
      // Bun 1.3's stop() promise does not settle once a WebSocket has been served; the listener closes regardless
      await Promise.race([server.stop(true), Bun.sleep(1000)]);
    },
  };
}
