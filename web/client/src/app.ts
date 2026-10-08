// app.ts - the shared state of the page: server config, capabilities, imported files, lobby connection.

import { PROTOCOL_VERSION, type IceServer } from "../../shared/protocol";
import { MAPS, MAX_PLAYERS_EXTENDED } from "../../shared/roster";
import { canPlayMap } from "./assets/manifest";
import { loadImported, type ImportedState } from "./assets/import";
import { opfsAvailable } from "./assets/opfs";
import { siteUrl, wsUrl } from "./base";
import { LobbyStore } from "./lobby/store";
import { ServerError, Signaling } from "./net/signaling";
import { NetTransport } from "./net/transport";
import type { Capability } from "./ui/capabilities";
import { toast } from "./ui/dom";

export interface AppConfig {
  iceServers: IceServer[];
  maxPlayers: number;
  engine: { available: boolean; version?: string };
  protocolVersion: number;
  loaded: boolean; // false when /api/config could not be fetched (defaults below)
}

export type ScreenId = "home" | "lobby" | "game" | "files" | "system";

const NAME_KEY = "bo1.name";

function defaultName(): string {
  return `Survivor${Math.floor(100 + Math.random() * 900)}`;
}

export function cleanName(n: string): string {
  return n.replace(/\s+/g, " ").trim().slice(0, 15);
}

export async function loadConfig(): Promise<AppConfig> {
  const fallback: AppConfig = {
    iceServers: [],
    maxPlayers: MAX_PLAYERS_EXTENDED,
    engine: { available: false },
    protocolVersion: PROTOCOL_VERSION,
    loaded: false,
  };
  try {
    const res = await fetch(siteUrl("api/config"), { cache: "no-cache" });
    if (!res.ok) throw new Error(String(res.status));
    const c = (await res.json()) as Partial<AppConfig>;
    return {
      iceServers: Array.isArray(c.iceServers) ? c.iceServers : [],
      maxPlayers: typeof c.maxPlayers === "number" ? c.maxPlayers : MAX_PLAYERS_EXTENDED,
      engine: { available: !!c.engine?.available, version: c.engine?.version },
      protocolVersion: typeof c.protocolVersion === "number" ? c.protocolVersion : PROTOCOL_VERSION,
      loaded: true,
    };
  } catch {
    // no config endpoint: look for the engine directly
    try {
      const res = await fetch(siteUrl("engine/bo1.js"), { method: "HEAD", cache: "no-cache" });
      fallback.engine.available = res.ok;
    } catch {
      /* offline */
    }
    return fallback;
  }
}

export class App {
  caps: Capability[] = [];
  files: ImportedState | null = null;
  screen: ScreenId = "home";
  name: string;
  readonly signaling: Signaling;
  readonly transport: NetTransport;
  readonly lobby: LobbyStore;
  private listeners = new Set<(what: string) => void>();
  private navigate: ((s: ScreenId) => void) | null = null;
  engineRunning = false;

  constructor(public config: AppConfig) {
    this.name = cleanName(localStorage.getItem(NAME_KEY) ?? "") || defaultName();
    localStorage.setItem(NAME_KEY, this.name);
    this.signaling = new Signaling(wsUrl("ws"), () => this.name);
    this.transport = new NetTransport(this.signaling);
    this.lobby = new LobbyStore(this.signaling, this.transport);
    this.signaling.onState(() => this.emit("connection"));
    // slot.files: tell the server whether we have the current map's files whenever the room or our files change
    this.lobby.on((e) => {
      if (e.type === "room") this.syncFilesFlag();
    });
    this.on((what) => {
      if (what === "files") this.syncFilesFlag();
    });
  }

  setNavigator(fn: (s: ScreenId) => void): void {
    this.navigate = fn;
  }

  go(screen: ScreenId): void {
    this.navigate?.(screen);
  }

  on(fn: (what: string) => void): () => void {
    this.listeners.add(fn);
    return () => this.listeners.delete(fn);
  }

  emit(what: string): void {
    for (const fn of [...this.listeners]) fn(what);
  }

  setName(n: string): void {
    const name = cleanName(n);
    if (!name || name === this.name) return;
    this.name = name;
    localStorage.setItem(NAME_KEY, name);
    this.signaling.send({ t: "rename", name });
    this.emit("name");
  }

  async refreshFiles(): Promise<void> {
    if (!opfsAvailable()) {
      this.files = null;
      this.emit("files");
      return;
    }
    try {
      this.files = await loadImported();
    } catch (e) {
      console.warn("loading import.json failed", e);
      this.files = null;
    }
    this.emit("files");
  }

  setFiles(state: ImportedState): void {
    this.files = state;
    this.emit("files");
  }

  hasFilesFor(mapId: string): boolean {
    return canPlayMap(this.files?.availability ?? null, mapId);
  }

  private syncFilesFlag(): void {
    const room = this.lobby.room;
    const me = this.lobby.mySlot;
    if (!room || !me) return;
    const has = this.hasFilesFor(room.settings.map);
    const maps = MAPS.filter((m) => this.hasFilesFor(m.id)).map((m) => m.id);
    // maps lets the server keep everyone's files flag right when the host changes the map
    if (me.hasGameFiles !== has || me.maps.join(" ") !== maps.join(" ")) {
      this.signaling.send({ t: "slot.files", hasGameFiles: has, maps });
    }
  }

  /** Runs a lobby request; server errors are already shown by the store, other failures are shown here. */
  async attempt<T>(p: Promise<T>): Promise<T | null> {
    try {
      return await p;
    } catch (e) {
      if (e instanceof ServerError && e.code !== "timeout" && e.code !== "offline") return null;
      toast(e instanceof Error ? e.message : String(e), "error");
      return null;
    }
  }
}
