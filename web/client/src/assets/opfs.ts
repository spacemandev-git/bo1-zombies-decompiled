// opfs.ts - Origin Private File System helpers. Everything the page stores lives under the OPFS directory bo1/:
//   bo1/game/...        the player's imported files + the repo's mods/ and main/ data (the engine's fs_b)
//   bo1/home/           the engine's fs_h
//   bo1/import.json     { files: { "<path under game/>": size }, maps: [...] } (docs/web-engine-interface.md)
//   bo1/data-index.json { files: { "<path>": { size, sha256 } } }  what syncData() copied

export const ROOT_DIR = "bo1";
export const GAME_DIR = "game";
export const IMPORT_INDEX = "import.json";
export const DATA_INDEX = "data-index.json";

export function opfsAvailable(): boolean {
  return typeof navigator !== "undefined" && typeof navigator.storage?.getDirectory === "function";
}

export async function opfsRoot(): Promise<FileSystemDirectoryHandle> {
  return navigator.storage.getDirectory();
}

/** bo1/<segments...> as a directory, created on demand. */
export async function getDir(segments: string[], create = true): Promise<FileSystemDirectoryHandle> {
  let dir = await (await opfsRoot()).getDirectoryHandle(ROOT_DIR, { create });
  for (const s of segments) dir = await dir.getDirectoryHandle(s, { create });
  return dir;
}

export function splitPath(path: string): string[] {
  return path.split("/").filter((s) => s.length > 0 && s !== "." && s !== "..");
}

/** The size of bo1/<path>, or null when it does not exist. */
export async function fileSize(path: string): Promise<number | null> {
  const segs = splitPath(path);
  const name = segs.pop();
  if (!name) return null;
  try {
    const dir = await getDir(segs, false);
    const fh = await dir.getFileHandle(name);
    return (await fh.getFile()).size;
  } catch {
    return null;
  }
}

export async function readText(path: string): Promise<string | null> {
  const segs = splitPath(path);
  const name = segs.pop();
  if (!name) return null;
  try {
    const dir = await getDir(segs, false);
    return await (await (await dir.getFileHandle(name)).getFile()).text();
  } catch {
    return null;
  }
}

export async function readJson<T>(path: string): Promise<T | null> {
  const t = await readText(path);
  if (t === null) return null;
  try {
    return JSON.parse(t) as T;
  } catch {
    return null;
  }
}

export async function removeEntry(path: string): Promise<void> {
  const segs = splitPath(path);
  const name = segs.pop();
  if (!name) return;
  try {
    const dir = await getDir(segs, false);
    await dir.removeEntry(name, { recursive: true });
  } catch {
    /* already gone */
  }
}

interface IterableDir {
  entries(): AsyncIterable<[string, FileSystemHandle]>;
}

/** Every file under bo1/<path> with its size (path relative to bo1/<path>). */
export async function listFiles(path: string): Promise<{ path: string; size: number }[]> {
  const out: { path: string; size: number }[] = [];
  let dir: FileSystemDirectoryHandle;
  try {
    dir = await getDir(splitPath(path), false);
  } catch {
    return out;
  }
  const walk = async (d: FileSystemDirectoryHandle, prefix: string): Promise<void> => {
    for await (const [name, handle] of (d as unknown as IterableDir).entries()) {
      if (handle.kind === "directory") await walk(handle as FileSystemDirectoryHandle, `${prefix}${name}/`);
      else out.push({ path: `${prefix}${name}`, size: (await (handle as FileSystemFileHandle).getFile()).size });
    }
  };
  await walk(dir, "");
  return out;
}

export interface StorageInfo {
  usage: number | null;
  quota: number | null;
  persisted: boolean | null;
}

export async function storageInfo(): Promise<StorageInfo> {
  const info: StorageInfo = { usage: null, quota: null, persisted: null };
  try {
    const e = await navigator.storage.estimate();
    info.usage = e.usage ?? null;
    info.quota = e.quota ?? null;
  } catch {
    /* unsupported */
  }
  try {
    info.persisted = await navigator.storage.persisted();
  } catch {
    /* unsupported */
  }
  return info;
}

export async function requestPersist(): Promise<boolean> {
  try {
    return await navigator.storage.persist();
  } catch {
    return false;
  }
}
