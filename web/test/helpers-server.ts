// helpers-server.ts - test helpers for the lobby (a fake clock, seeded randomness, a capturing harness) and for
// talking to a running server (raw HTTP requests, a WebSocket client with a message queue).

import net from "node:net";
import type { ClientMessage, RoomState, ServerMessage } from "../shared/protocol";
import { Lobby, type ConnId, type LobbyLimits, type Outgoing } from "../server/lobby";

export class FakeClock {
  t = 1_700_000_000_000;
  now = (): number => this.t;
  advance(ms: number): void {
    this.t += ms;
  }
}

/** Deterministic random bytes (xorshift32). */
export function seededRandom(seed = 0x9e3779b9): (n: number) => Uint8Array {
  let x = seed >>> 0 || 1;
  return (n) => {
    const out = new Uint8Array(n);
    for (let i = 0; i < n; i++) {
      x ^= x << 13;
      x >>>= 0;
      x ^= x >>> 17;
      x ^= x << 5;
      x >>>= 0;
      out[i] = x & 0xff;
    }
    return out;
  };
}

export interface Player {
  conn: ConnId;
  peerId: string;
  token: string;
  name: string;
}

type Msg<T extends ServerMessage["t"]> = Extract<ServerMessage, { t: T }>;

export class LobbyHarness {
  readonly clock = new FakeClock();
  readonly lobby: Lobby;
  readonly outbox = new Map<ConnId, Outgoing[]>();
  readonly disconnects: { conn: ConnId; code: number; reason: string }[] = [];
  private nextConn = 1;

  constructor(limits: Partial<LobbyLimits> = {}) {
    this.lobby = new Lobby({
      now: this.clock.now,
      randomBytes: seededRandom(),
      iceServers: [{ urls: ["stun:stun.example:3478"] }],
      limits,
      send: (conn, msg) => {
        const box = this.outbox.get(conn) ?? [];
        box.push(msg);
        this.outbox.set(conn, box);
      },
      disconnect: (conn, code, reason) => this.disconnects.push({ conn, code, reason }),
    });
  }

  connect(): ConnId {
    const conn = this.nextConn++;
    this.outbox.set(conn, []);
    this.lobby.open(conn);
    return conn;
  }

  send(conn: ConnId, msg: ClientMessage | Record<string, unknown>): void {
    this.lobby.message(conn, JSON.stringify(msg));
  }

  raw(conn: ConnId, data: string | Uint8Array): void {
    this.lobby.message(conn, data);
  }

  /** Everything sent to `conn` since the last take(). */
  take(conn: ConnId): Outgoing[] {
    const box = this.outbox.get(conn) ?? [];
    this.outbox.set(conn, []);
    return box;
  }

  messages(conn: ConnId): ServerMessage[] {
    return (this.outbox.get(conn) ?? []).filter((m): m is ServerMessage => !(m instanceof Uint8Array));
  }

  frames(conn: ConnId): Uint8Array[] {
    return (this.outbox.get(conn) ?? []).filter((m): m is Uint8Array => m instanceof Uint8Array);
  }

  last<T extends ServerMessage["t"]>(conn: ConnId, t: T): Msg<T> | undefined {
    const all = this.messages(conn).filter((m) => m.t === t);
    return all[all.length - 1] as Msg<T> | undefined;
  }

  lastError(conn: ConnId): Msg<"error"> | undefined {
    return this.last(conn, "error");
  }

  state(conn: ConnId): RoomState {
    const s = this.last(conn, "room.state");
    if (!s) throw new Error(`no room.state for conn ${conn}`);
    return s.room;
  }

  player(name: string, resumeToken?: string): Player {
    const conn = this.connect();
    this.send(conn, resumeToken ? { t: "hello", version: 1, name, resumeToken } : { t: "hello", version: 1, name });
    const w = this.last(conn, "welcome");
    if (!w) throw new Error(`no welcome for ${name}: ${JSON.stringify(this.messages(conn))}`);
    return { conn, peerId: w.peerId, token: w.resumeToken, name };
  }

  create(p: Player, settings: Record<string, unknown> = {}): string {
    this.send(p.conn, { t: "room.create", settings });
    const s = this.last(p.conn, "room.state");
    if (!s) throw new Error(`room.create failed: ${JSON.stringify(this.messages(p.conn))}`);
    return s.room.code;
  }

  join(p: Player, code: string): void {
    this.send(p.conn, { t: "room.join", code });
  }

  /** Marks the player ready with game files. */
  readyUp(p: Player): void {
    this.send(p.conn, { t: "slot.files", hasGameFiles: true });
    this.send(p.conn, { t: "slot.ready", ready: true });
  }
}

/** Sends one raw HTTP/1.1 GET (the target exactly as given, which fetch would normalize) and returns status, head, body. */
export function rawRequest(
  port: number,
  target: string,
  headers: Record<string, string> = {},
): Promise<{ status: number; head: string; body: string }> {
  return new Promise((resolve, reject) => {
    const socket = net.connect(port, "127.0.0.1", () => {
      const lines = [`GET ${target} HTTP/1.1`, `Host: localhost:${port}`, "Connection: close"];
      for (const [k, v] of Object.entries(headers)) lines.push(`${k}: ${v}`);
      socket.write(lines.join("\r\n") + "\r\n\r\n");
    });
    let data = "";
    let done = false;
    // Bun 1.3 keeps the connection open after an async response despite "Connection: close", so the response is
    // complete once Content-Length bytes of body (or the end of the stream) have arrived
    const finish = () => {
      if (done) return;
      done = true;
      socket.destroy();
      const i = data.indexOf("\r\n\r\n");
      const head = i >= 0 ? data.slice(0, i) : data;
      const status = Number(/^HTTP\/1\.1 (\d{3})/.exec(head)?.[1] ?? 0);
      resolve({ status, head, body: i >= 0 ? data.slice(i + 4) : "" });
    };
    socket.setEncoding("latin1");
    socket.on("data", (c) => {
      data += c;
      const i = data.indexOf("\r\n\r\n");
      if (i < 0) return;
      const len = /^content-length:\s*(\d+)/im.exec(data.slice(0, i));
      const status = Number(/^HTTP\/1\.1 (\d{3})/.exec(data)?.[1] ?? 0);
      const bodyless = status === 101 || status === 204 || status === 304;
      if (bodyless || (len && data.length - (i + 4) >= Number(len[1]))) finish();
    });
    socket.on("error", (e) => (done ? undefined : reject(e)));
    socket.on("end", finish);
  });
}

/** A WebSocket client that queues what it receives and can wait for a matching message. */
export class WsClient {
  readonly received: (ServerMessage | Uint8Array)[] = [];
  private waiters: { pred: (m: ServerMessage | Uint8Array) => boolean; resolve: (m: ServerMessage | Uint8Array) => void }[] = [];
  closed: { code: number; reason: string } | null = null;

  private constructor(readonly ws: WebSocket) {
    ws.binaryType = "arraybuffer";
    ws.addEventListener("message", (ev) => {
      const m: ServerMessage | Uint8Array =
        typeof ev.data === "string" ? (JSON.parse(ev.data) as ServerMessage) : new Uint8Array(ev.data as ArrayBuffer);
      this.received.push(m);
      for (const w of [...this.waiters]) {
        if (w.pred(m)) {
          this.waiters.splice(this.waiters.indexOf(w), 1);
          w.resolve(m);
        }
      }
    });
    ws.addEventListener("close", (ev) => (this.closed = { code: ev.code, reason: ev.reason }));
  }

  static open(url: string, headers?: Record<string, string>): Promise<WsClient> {
    return new Promise((resolve, reject) => {
      // Bun's WebSocket accepts a headers option (not in the DOM typings)
      const ws = new WebSocket(url, headers ? ({ headers } as unknown as string[]) : undefined);
      const c = new WsClient(ws);
      ws.addEventListener("open", () => resolve(c), { once: true });
      ws.addEventListener("error", () => reject(new Error(`WebSocket ${url} failed`)), { once: true });
    });
  }

  send(msg: ClientMessage): void {
    this.ws.send(JSON.stringify(msg));
  }

  sendBinary(data: Uint8Array): void {
    this.ws.send(data);
  }

  /** Resolves with the first message (already received or future) matching pred. */
  waitFor<M extends ServerMessage | Uint8Array>(pred: (m: ServerMessage | Uint8Array) => boolean, timeoutMs = 3000): Promise<M> {
    const seen = this.received.find(pred);
    if (seen) {
      this.received.splice(this.received.indexOf(seen), 1);
      return Promise.resolve(seen as M);
    }
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error(`timed out waiting; received ${JSON.stringify(this.received)}`)), timeoutMs);
      this.waiters.push({
        pred,
        resolve: (m) => {
          clearTimeout(timer);
          this.received.splice(this.received.indexOf(m), 1);
          resolve(m as M);
        },
      });
    });
  }

  waitForType<T extends ServerMessage["t"]>(t: T, pred: (m: Msg<T>) => boolean = () => true): Promise<Msg<T>> {
    return this.waitFor<Msg<T>>((m) => !(m instanceof Uint8Array) && m.t === t && pred(m as Msg<T>));
  }

  close(): void {
    this.ws.close();
  }
}
