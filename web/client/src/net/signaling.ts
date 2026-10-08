// signaling.ts - the lobby WebSocket client (protocol: web/shared/protocol.ts).
//
// - sends hello (PROTOCOL_VERSION, name, resumeToken from sessionStorage) on every (re)connect;
// - reconnects with exponential backoff and resumes the session with the token from the last welcome;
// - typed events per ServerMessage type, request helpers that wait for the matching reply or an error;
// - ping/pong every 5 s for the RTT to the server;
// - binary frames: relayed game packets [RELAY_FRAME][slot][payload].

import {
  PROTOCOL_VERSION,
  RELAY_FRAME,
  type ClientMessage,
  type IceServer,
  type RoomSettings,
  type RoomState,
  type RoomSummary,
  type ServerMessage,
} from "../../../shared/protocol";

export type ServerMessageOf<T extends ServerMessage["t"]> = Extract<ServerMessage, { t: T }>;
export type ConnState = "connecting" | "open" | "closed";

const RESUME_KEY = "bo1.resumeToken";
const PING_INTERVAL_MS = 5000;
/** Close code the server uses when a newer connection took over this session. */
const CLOSE_REPLACED = 4000;

export class ServerError extends Error {
  constructor(
    readonly code: string,
    message: string,
  ) {
    super(message);
  }
}

type AnyHandler = (msg: ServerMessage) => void;

export class Signaling {
  state: ConnState = "closed";
  peerId: string | null = null;
  iceServers: IceServer[] = [];
  rttMs: number | null = null;
  /** true after a "version" error: the page must be reloaded */
  outdated = false;

  private ws: WebSocket | null = null;
  private handlers = new Map<string, Set<AnyHandler>>();
  private stateHandlers = new Set<(s: ConnState) => void>();
  private binaryHandlers = new Set<(fromSlot: number, data: Uint8Array<ArrayBuffer>) => void>();
  private attempt = 0;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private pingTimer: ReturnType<typeof setInterval> | null = null;
  private stopped = false;

  constructor(
    private readonly url: string,
    private readonly getName: () => string,
  ) {}

  connect(): void {
    this.stopped = false;
    if (this.ws && (this.ws.readyState === WebSocket.OPEN || this.ws.readyState === WebSocket.CONNECTING)) return;
    this.setState("connecting");
    let ws: WebSocket;
    try {
      ws = new WebSocket(this.url);
    } catch {
      this.scheduleReconnect();
      return;
    }
    ws.binaryType = "arraybuffer";
    this.ws = ws;
    ws.onopen = () => {
      const resumeToken = sessionStorage.getItem(RESUME_KEY) ?? undefined;
      this.rawSend({ t: "hello", version: PROTOCOL_VERSION, name: this.getName(), resumeToken });
    };
    ws.onmessage = (ev) => this.onMessage(ev.data);
    ws.onclose = (ev) => {
      if (this.ws !== ws) return;
      this.ws = null;
      this.stopPing();
      // 4000: another connection resumed this session (a duplicated tab copies sessionStorage): start a new one
      if (ev.code === CLOSE_REPLACED) this.forgetSession();
      this.setState("closed");
      if (!this.stopped && !this.outdated) this.scheduleReconnect();
    };
    ws.onerror = () => {
      /* onclose follows */
    };
  }

  close(): void {
    this.stopped = true;
    if (this.reconnectTimer) clearTimeout(this.reconnectTimer);
    this.ws?.close();
  }

  /** Drops the stored session (next connect starts a fresh one). */
  forgetSession(): void {
    sessionStorage.removeItem(RESUME_KEY);
  }

  private scheduleReconnect(): void {
    if (this.reconnectTimer) return;
    const delay = Math.min(10_000, 500 * 2 ** this.attempt) * (0.75 + Math.random() * 0.5);
    this.attempt++;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      this.connect();
    }, delay);
  }

  private setState(s: ConnState): void {
    if (this.state === s) return;
    this.state = s;
    for (const fn of this.stateHandlers) fn(s);
  }

  private startPing(): void {
    this.stopPing();
    const ping = () => this.send({ t: "ping", time: performance.now() });
    ping();
    this.pingTimer = setInterval(ping, PING_INTERVAL_MS);
  }

  private stopPing(): void {
    if (this.pingTimer) clearInterval(this.pingTimer);
    this.pingTimer = null;
  }

  private onMessage(data: unknown): void {
    if (data instanceof ArrayBuffer) {
      const bytes = new Uint8Array(data);
      if (bytes.length >= 2 && bytes[0] === RELAY_FRAME) {
        const payload = bytes.slice(2);
        for (const fn of this.binaryHandlers) fn(bytes[1]!, payload);
      }
      return;
    }
    if (typeof data !== "string") return;
    let msg: ServerMessage;
    try {
      msg = JSON.parse(data) as ServerMessage;
    } catch {
      return;
    }
    if (!msg || typeof msg !== "object" || typeof (msg as { t?: unknown }).t !== "string") return;
    switch (msg.t) {
      case "welcome":
        this.peerId = msg.peerId;
        this.iceServers = msg.iceServers ?? [];
        sessionStorage.setItem(RESUME_KEY, msg.resumeToken);
        this.attempt = 0;
        this.setState("open");
        this.startPing();
        break;
      case "pong":
        this.rttMs = Math.max(0, performance.now() - msg.time);
        break;
      case "error":
        if (msg.code === "version") {
          this.outdated = true;
          this.stopped = true;
        }
        break;
    }
    this.emit(msg);
  }

  private emit(msg: ServerMessage): void {
    for (const key of [msg.t, "*"]) {
      const set = this.handlers.get(key);
      if (set) for (const fn of [...set]) fn(msg);
    }
  }

  /** Subscribes to one message type ("*" for all). Returns the unsubscribe function. */
  on<T extends ServerMessage["t"]>(type: T, fn: (msg: ServerMessageOf<T>) => void): () => void;
  on(type: "*", fn: (msg: ServerMessage) => void): () => void;
  on(type: string, fn: (msg: never) => void): () => void {
    let set = this.handlers.get(type);
    if (!set) {
      set = new Set();
      this.handlers.set(type, set);
    }
    const h = fn as unknown as AnyHandler;
    set.add(h);
    return () => set.delete(h);
  }

  onState(fn: (s: ConnState) => void): () => void {
    this.stateHandlers.add(fn);
    return () => this.stateHandlers.delete(fn);
  }

  onRelay(fn: (fromSlot: number, data: Uint8Array<ArrayBuffer>) => void): () => void {
    this.binaryHandlers.add(fn);
    return () => this.binaryHandlers.delete(fn);
  }

  get open(): boolean {
    return this.state === "open" && this.ws?.readyState === WebSocket.OPEN;
  }

  private rawSend(msg: ClientMessage): boolean {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return false;
    this.ws.send(JSON.stringify(msg));
    return true;
  }

  /** Sends a message once the session is open (after welcome). Returns false when not connected. */
  send(msg: ClientMessage): boolean {
    if (this.state !== "open") return false;
    return this.rawSend(msg);
  }

  /** Sends a game packet through the server relay to `toSlot` in the same room. */
  sendRelay(toSlot: number, payload: Uint8Array): boolean {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN || this.state !== "open") return false;
    if (this.ws.bufferedAmount > 1 << 20) return false; // backed up: drop like UDP would
    const frame = new Uint8Array(payload.length + 2);
    frame[0] = RELAY_FRAME;
    frame[1] = toSlot & 0xff;
    frame.set(payload, 2);
    this.ws.send(frame);
    return true;
  }

  /**
   * Sends `msg` and waits for the first reply of one of `replyTypes`, or rejects on an error message or timeout.
   * (The protocol has no request ids; replies are matched by type.)
   */
  request<T extends ServerMessage["t"]>(msg: ClientMessage, replyTypes: T[], timeoutMs = 8000): Promise<ServerMessageOf<T>> {
    return new Promise((resolve, reject) => {
      const offs: (() => void)[] = [];
      const done = () => {
        clearTimeout(timer);
        for (const off of offs) off();
      };
      const timer = setTimeout(() => {
        done();
        reject(new ServerError("timeout", "The server did not answer."));
      }, timeoutMs);
      for (const t of replyTypes) {
        offs.push(
          this.on(t, (m) => {
            done();
            resolve(m as ServerMessageOf<T>);
          }),
        );
      }
      offs.push(
        this.on("error", (m) => {
          done();
          reject(new ServerError(m.code, m.message));
        }),
      );
      if (!this.send(msg)) {
        done();
        reject(new ServerError("offline", "Not connected to the server."));
      }
    });
  }

  async createRoom(settings: Partial<RoomSettings>): Promise<RoomState> {
    const m = await this.request({ t: "room.create", settings }, ["room.state"]);
    return m.room;
  }

  async joinRoom(code: string): Promise<RoomState> {
    const m = await this.request({ t: "room.join", code: code.trim().toUpperCase() }, ["room.state"]);
    return m.room;
  }

  async listRooms(): Promise<RoomSummary[]> {
    const m = await this.request({ t: "room.list" }, ["room.list"]);
    return m.rooms;
  }
}
