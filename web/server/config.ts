// config.ts - the server's settings, read from environment variables (see web/README.md for the list).

import path from "node:path";
import type { IceServer } from "../shared/protocol";
import type { LogLevel } from "./log";

export interface ServerConfig {
  port: number;
  /** "" (served at the root) or "/x[/y...]": leading slash, no trailing slash. */
  basePath: string;
  /** GET "/" answers 302 to basePath + "/" (only meaningful when basePath is not ""). */
  rootRedirect: boolean;
  publicDir: string;
  engineDir: string;
  /** The server publishes <dataRoot>/mods/** as mods/** and <dataRoot>/data/main/** as main/**. */
  dataRoot: string;
  iceServers: IceServer[];
  maxRooms: number;
  logLevel: LogLevel;
}

/** The web/ directory (this file is web/server/config.ts). */
export const WEB_DIR = path.resolve(import.meta.dir, "..");

export const DEFAULT_STUN_URLS = "stun:stun.l.google.com:19302,stun:stun1.l.google.com:19302";

export function normalizeBasePath(value: string): string {
  let p = value.trim();
  if (p === "" || p === "/") return "";
  if (!p.startsWith("/")) p = "/" + p;
  p = p.replace(/\/+$/, "");
  if (!/^(\/[A-Za-z0-9._~-]+)+$/.test(p) || p.split("/").some((s) => s === "." || s === "..")) {
    throw new Error(`BASE_PATH ${JSON.stringify(value)} is not a valid URL path (letters, digits, . _ ~ - and /)`);
  }
  return p;
}

function parseBool(name: string, value: string | undefined, fallback: boolean): boolean {
  if (value === undefined || value.trim() === "") return fallback;
  const v = value.trim().toLowerCase();
  if (["1", "true", "yes", "on"].includes(v)) return true;
  if (["0", "false", "no", "off"].includes(v)) return false;
  throw new Error(`${name} must be true or false, not ${JSON.stringify(value)}`);
}

function parseInteger(name: string, value: string | undefined, fallback: number, min: number, max: number): number {
  if (value === undefined || value.trim() === "") return fallback;
  const n = Number(value.trim());
  if (!Number.isInteger(n) || n < min || n > max) throw new Error(`${name} must be an integer ${min}..${max}, not ${JSON.stringify(value)}`);
  return n;
}

function splitList(value: string): string[] {
  return value
    .split(",")
    .map((s) => s.trim())
    .filter((s) => s !== "");
}

export function parseIceServers(env: Record<string, string | undefined>): IceServer[] {
  const servers: IceServer[] = [];
  const stun = splitList(env.STUN_URLS ?? DEFAULT_STUN_URLS);
  for (const u of stun) {
    if (!/^stuns?:\S+$/.test(u)) throw new Error(`STUN_URLS entry ${JSON.stringify(u)} must start with stun: or stuns:`);
  }
  if (stun.length) servers.push({ urls: stun });
  const turn = splitList(env.TURN_URL ?? "");
  if (turn.length) {
    for (const u of turn) {
      if (!/^turns?:\S+$/.test(u)) throw new Error(`TURN_URL entry ${JSON.stringify(u)} must start with turn: or turns:`);
    }
    const server: IceServer = { urls: turn };
    if (env.TURN_USERNAME) server.username = env.TURN_USERNAME;
    if (env.TURN_CREDENTIAL) server.credential = env.TURN_CREDENTIAL;
    servers.push(server);
  }
  return servers;
}

const LOG_LEVELS: LogLevel[] = ["debug", "info", "warn", "error", "silent"];

export function loadConfig(env: Record<string, string | undefined> = process.env, webDir = WEB_DIR): ServerConfig {
  const logLevel = (env.LOG_LEVEL?.trim().toLowerCase() || "info") as LogLevel;
  if (!LOG_LEVELS.includes(logLevel)) throw new Error(`LOG_LEVEL must be one of ${LOG_LEVELS.join(", ")}`);
  const resolveDir = (v: string | undefined, fallback: string) => path.resolve(v?.trim() ? v.trim() : fallback);
  return {
    port: parseInteger("PORT", env.PORT, 8080, 0, 65535),
    basePath: normalizeBasePath(env.BASE_PATH ?? "/bo1"),
    rootRedirect: parseBool("ROOT_REDIRECT", env.ROOT_REDIRECT, true),
    publicDir: resolveDir(env.PUBLIC_DIR, path.join(webDir, "dist", "public")),
    engineDir: resolveDir(env.ENGINE_DIR, path.join(webDir, "engine")),
    dataRoot: resolveDir(env.DATA_ROOT, path.resolve(webDir, "..")),
    iceServers: parseIceServers(env),
    maxRooms: parseInteger("MAX_ROOMS", env.MAX_ROOMS, 500, 1, 100_000),
    logLevel,
  };
}
