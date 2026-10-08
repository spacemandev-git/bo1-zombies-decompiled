// manifest.ts - the data manifest (docs/web-engine-interface.md section 2).
//
// Everything under <dataRoot>/mods/ is published as mods/..., everything under <dataRoot>/data/main/ as main/...
// Dotfiles and dot-directories are skipped, and so are symlinks (nothing outside the two trees can be published).

import { createHash } from "node:crypto";
import { readdir, readFile } from "node:fs/promises";
import path from "node:path";

export interface ManifestEntry {
  path: string;
  size: number;
  sha256: string;
}

export interface DataManifest {
  files: ManifestEntry[];
  /** published path -> absolute file path */
  byPath: Map<string, string>;
  json: string;
}

const TREES: { dir: string[]; prefix: string }[] = [
  { dir: ["mods"], prefix: "mods" },
  { dir: ["data", "main"], prefix: "main" },
];

async function walk(dir: string, rel: string, out: { abs: string; rel: string }[]): Promise<void> {
  let entries;
  try {
    entries = await readdir(dir, { withFileTypes: true });
  } catch (e) {
    if ((e as NodeJS.ErrnoException).code === "ENOENT") return;
    throw e;
  }
  for (const entry of entries) {
    if (entry.name.startsWith(".")) continue;
    const abs = path.join(dir, entry.name);
    const r = `${rel}/${entry.name}`;
    if (entry.isDirectory()) await walk(abs, r, out);
    else if (entry.isFile()) out.push({ abs, rel: r });
  }
}

export async function buildManifest(dataRoot: string): Promise<DataManifest> {
  const found: { abs: string; rel: string }[] = [];
  for (const tree of TREES) await walk(path.join(dataRoot, ...tree.dir), tree.prefix, found);
  found.sort((a, b) => (a.rel < b.rel ? -1 : a.rel > b.rel ? 1 : 0));
  const files: ManifestEntry[] = [];
  const byPath = new Map<string, string>();
  for (const f of found) {
    const bytes = await readFile(f.abs);
    files.push({ path: f.rel, size: bytes.length, sha256: createHash("sha256").update(bytes).digest("hex") });
    byPath.set(f.rel, f.abs);
  }
  return { files, byPath, json: JSON.stringify({ files }) };
}
