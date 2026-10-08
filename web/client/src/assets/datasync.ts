// datasync.ts - copies the repo's mods/ and data/main files the server publishes into OPFS bo1/game/ before a game.
//
// <base>/data/manifest.json lists { path, size, sha256 } (paths relative to the game root: mods/..., main/...).
// bo1/data-index.json remembers what was copied, so only new or changed files are downloaded; files that left the
// manifest are deleted. Paths that are player game files (zones, iwds, localization.txt) are never touched.

import { dataUrl, siteUrl } from "../base";
import { writeBlob, writeText } from "./copy";
import { classifyGamePath, normalizePath } from "./manifest";
import { DATA_INDEX, GAME_DIR, listFiles, readJson, removeEntry } from "./opfs";

export interface DataFile {
  path: string;
  size: number;
  sha256: string;
}

interface DataIndex {
  files: Record<string, { size: number; sha256: string }>;
}

export interface SyncProgress {
  done: number;
  total: number;
  file: string | null;
}

export interface SyncResult {
  total: number;
  updated: number;
  removed: number;
}

/** Only mods/... and main/... paths without "." / ".." segments, and never a player game file. */
export function safeDataPath(path: string): boolean {
  const p = normalizePath(path);
  const segs = p.split("/");
  if (segs.some((s) => s === "" || s === "." || s === "..")) return false;
  const top = segs[0]?.toLowerCase();
  if (top !== "mods" && top !== "main") return false;
  return classifyGamePath(p, { includeVideo: true }) === null;
}

async function sha256Hex(buf: ArrayBuffer): Promise<string | null> {
  if (!globalThis.crypto?.subtle) return null;
  const d = await crypto.subtle.digest("SHA-256", buf);
  return [...new Uint8Array(d)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

export async function fetchDataManifest(): Promise<DataFile[]> {
  const res = await fetch(siteUrl("data/manifest.json"), { cache: "no-cache" });
  if (!res.ok) throw new Error(`data/manifest.json: HTTP ${res.status}`);
  const m = (await res.json()) as { files?: DataFile[] };
  return Array.isArray(m.files) ? m.files : [];
}

export async function syncData(onProgress?: (p: SyncProgress) => void, signal?: AbortSignal): Promise<SyncResult> {
  const files = (await fetchDataManifest()).filter((f) => safeDataPath(f.path));
  const index: DataIndex = (await readJson<DataIndex>(DATA_INDEX)) ?? { files: {} };
  if (!index.files || typeof index.files !== "object") index.files = {};
  const existing = new Map<string, number>();
  for (const f of await listFiles(GAME_DIR)) existing.set(f.path.toLowerCase(), f.size);
  const todo = files.filter((f) => {
    const prev = index.files[f.path];
    return !(prev && prev.sha256 === f.sha256 && existing.get(normalizePath(f.path).toLowerCase()) === f.size);
  });
  let done = 0;
  onProgress?.({ done, total: todo.length, file: null });
  for (const f of todo) {
    if (signal?.aborted) throw new DOMException("cancelled", "AbortError");
    onProgress?.({ done, total: todo.length, file: f.path });
    const res = await fetch(dataUrl(normalizePath(f.path)), { cache: "no-cache", signal });
    if (!res.ok) throw new Error(`${f.path}: HTTP ${res.status}`);
    const buf = await res.arrayBuffer();
    const hash = await sha256Hex(buf);
    if (hash && f.sha256 && hash !== f.sha256.toLowerCase()) throw new Error(`${f.path}: checksum mismatch`);
    await writeBlob(`${GAME_DIR}/${normalizePath(f.path)}`, new Blob([buf]), undefined, signal);
    index.files[f.path] = { size: f.size, sha256: f.sha256 };
    done++;
  }
  const keep = new Set(files.map((f) => f.path));
  let removed = 0;
  for (const p of Object.keys(index.files)) {
    if (keep.has(p)) continue;
    if (safeDataPath(p)) await removeEntry(`${GAME_DIR}/${normalizePath(p)}`);
    delete index.files[p];
    removed++;
  }
  await writeText(DATA_INDEX, JSON.stringify(index));
  onProgress?.({ done, total: todo.length, file: null });
  return { total: files.length, updated: done, removed };
}
