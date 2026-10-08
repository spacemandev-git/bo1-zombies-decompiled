// manifest.ts - which of the player's Black Ops files Zombies needs, and how a picked folder maps onto them.
//
// Pure functions only (no DOM, no OPFS) so they are unit-tested in web/test/client-manifest.test.ts.
//
// Layout of a retail install (case varies between installs; the engine resolves paths case-insensitively):
//   localization.txt                         first line = the install's language ("english")
//   zone/Common/<zone>.ff                    unlocalized zones
//   zone/<Language>/<prefix><zone>.ff        localized zones, e.g. zone/English/en_patch_mp.ff
//   main/iw_*.iwd, main/localized_<language>_iwd*.iwd
//   main/video/*.bik                         cinematics (not used by the web build yet)
//
// Files are copied to OPFS bo1/game/<dest>, where dest uses the retail directory names (zone, Common, English, main,
// video) and the file's own name as found in the install.

import { MAPS } from "../../../shared/roster";

export interface Language {
  name: string; // as in localization.txt and the engine's language table (src/stringed/stringed_hooks.cpp)
  prefix: string; // localized zone prefix
  folder: string; // zone/<folder>
}

// src/stringed/stringed_hooks.cpp g_languages; german and austrian share the german folder (DB_IsUsingGermanPaths)
export const LANGUAGES: Language[] = [
  { name: "english", prefix: "en_", folder: "English" },
  { name: "french", prefix: "fr_", folder: "French" },
  { name: "frenchcan", prefix: "fc_", folder: "FrenchCan" },
  { name: "german", prefix: "ge_", folder: "German" },
  { name: "italian", prefix: "it_", folder: "Italian" },
  { name: "spanish", prefix: "sp_", folder: "Spanish" },
  { name: "british", prefix: "br_", folder: "British" },
  { name: "russian", prefix: "ru_", folder: "Russian" },
  { name: "polish", prefix: "po_", folder: "Polish" },
  { name: "korean", prefix: "ko_", folder: "Korean" },
  { name: "japanese", prefix: "ja_", folder: "Japanese" },
  { name: "czech", prefix: "cz_", folder: "Czech" },
];

export function findLanguage(name: string | null | undefined): Language | undefined {
  if (!name) return undefined;
  const n = name.trim().toLowerCase();
  if (n === "austrian") return LANGUAGES.find((l) => l.name === "german");
  return LANGUAGES.find((l) => l.name === n);
}

/** Zones every Zombies game loads (zone/Common/<name>.ff). */
export const CORE_ZONES = [
  "code_pre_gfx_mp",
  "code_post_gfx_mp",
  "code_post_gfx",
  "patch_mp",
  "patch",
  "ui_mp",
  "frontend",
  "common_zombie",
  "common_zombie_patch",
] as const;

/**
 * Localized core zones that must exist (zone/<Language>/<prefix><name>.ff). The engine also asks for the localized
 * version of every other zone, but treats a missing one as a warning, and only these are known to ship with retail
 * (src/database/db_registry.cpp POOLSIZE_LOCALIZE notes). The others are imported when present.
 */
export const CORE_LOCALIZED_REQUIRED = [
  "code_pre_gfx_mp",
  "code_post_gfx_mp",
  "code_post_gfx",
  "patch_mp",
  "patch",
  "common_zombie",
] as const;

/** Zones imported when present but never required. */
export const OPTIONAL_ZONES = ["patch_ui", "patch_ui_mp", "dev_mp"] as const;

export type FileKind = "core-zone" | "map-zone" | "optional-zone" | "iwd" | "localized-iwd" | "localization" | "video";

export interface GameFileInfo {
  dest: string; // canonical path under the game root (bo1/game/<dest>)
  kind: FileKind;
  zone?: string; // zone name without the language prefix and ".ff"
  map?: string; // map id for map zones
  language?: string; // localized zones and iwds
  localized?: boolean;
}

export interface ClassifyOptions {
  includeVideo?: boolean;
}

export function normalizePath(p: string): string {
  return p.replace(/\\/g, "/").replace(/\/{2,}/g, "/").replace(/^\/+/, "").replace(/\/+$/, "");
}

function zoneKind(name: string): { kind: FileKind; map?: string } | null {
  if ((CORE_ZONES as readonly string[]).includes(name)) return { kind: "core-zone" };
  if ((OPTIONAL_ZONES as readonly string[]).includes(name)) return { kind: "optional-zone" };
  for (const m of MAPS) {
    if (name === m.id || name === `${m.id}_patch`) return { kind: "map-zone", map: m.id };
  }
  return null;
}

/**
 * Classifies a path relative to the game root (case-insensitive, "\" or "/"). Returns null for every file Zombies
 * does not need (mp_* and so_* zones, campaign zones, executables, Redist, ...).
 */
export function classifyGamePath(rel: string, opts: ClassifyOptions = {}): GameFileInfo | null {
  const segs = normalizePath(rel).split("/").filter((s) => s.length > 0);
  const lower = segs.map((s) => s.toLowerCase());
  const [s0, s1, s2] = segs;
  const [l0, l1, l2] = lower;
  if (segs.length === 1 && l0 === "localization.txt" && s0) {
    return { dest: s0, kind: "localization" };
  }
  if (segs.length === 3 && l0 === "zone" && l1 && l2 && s2 && l2.endsWith(".ff")) {
    const base = l2.slice(0, -3);
    if (l1 === "common") {
      const k = zoneKind(base);
      if (!k) return null;
      return { dest: `zone/Common/${s2}`, kind: k.kind, zone: base, map: k.map, localized: false };
    }
    const lang = findLanguage(l1);
    if (!lang || !base.startsWith(lang.prefix)) return null;
    const zone = base.slice(lang.prefix.length);
    const k = zoneKind(zone);
    if (!k) return null;
    return {
      dest: `zone/${lang.folder}/${s2}`,
      kind: k.kind,
      zone,
      map: k.map,
      language: lang.name,
      localized: true,
    };
  }
  if (l0 === "main" && segs.length === 2 && l1 && s1) {
    if (/^iw_[^/]*\.iwd$/.test(l1)) return { dest: `main/${s1}`, kind: "iwd" };
    const m = /^localized_([a-z]+)_iwd[^/]*\.iwd$/.exec(l1);
    if (m && m[1]) return { dest: `main/${s1}`, kind: "localized-iwd", language: m[1], localized: true };
    return null;
  }
  if (l0 === "main" && segs.length === 3 && l1 === "video" && l2 && s2 && l2.endsWith(".bik")) {
    return opts.includeVideo ? { dest: `main/video/${s2}`, kind: "video" } : null;
  }
  return null;
}

/**
 * Finds the install root inside a picked selection: the directory that holds zone/ and main/ (and
 * localization.txt). Paths are relative to the selection and start with the picked folder's own name, so picking
 * the install folder, its parent, or zone/ or main/ themselves all work. Returns "" when the selection itself is the
 * root's content (the user picked zone/ or main/), a prefix without trailing slash otherwise, null when no
 * candidate exists.
 */
export function detectRoot(paths: string[]): string | null {
  interface Score {
    zone: boolean;
    main: boolean;
    loc: boolean;
    files: number;
  }
  const scores = new Map<string, Score>();
  const get = (key: string): Score => {
    let s = scores.get(key);
    if (!s) {
      s = { zone: false, main: false, loc: false, files: 0 };
      scores.set(key, s);
    }
    return s;
  };
  for (const raw of paths) {
    const segs = normalizePath(raw).split("/");
    for (let i = 0; i < segs.length - 1; i++) {
      const l = segs[i]!.toLowerCase();
      if (l === "zone" || l === "main") {
        const s = get(segs.slice(0, i).join("/"));
        if (l === "zone") s.zone = true;
        else s.main = true;
        s.files++;
      }
    }
    if (segs[segs.length - 1]!.toLowerCase() === "localization.txt") {
      const s = get(segs.slice(0, -1).join("/"));
      s.loc = true;
      s.files++;
    }
  }
  let best: string | null = null;
  let bestRank: [number, number, number] = [-1, -1, 0];
  for (const [key, s] of scores) {
    const rank: [number, number, number] = [
      (s.zone ? 2 : 0) + (s.main ? 2 : 0) + (s.loc ? 1 : 0),
      s.files,
      -key.length,
    ];
    if (
      rank[0] > bestRank[0] ||
      (rank[0] === bestRank[0] && (rank[1] > bestRank[1] || (rank[1] === bestRank[1] && rank[2] > bestRank[2])))
    ) {
      best = key;
      bestRank = rank;
    }
  }
  return best;
}

export interface PickedFile {
  path: string; // relative to the selection, starting with the picked folder's name
  size: number;
}

export interface MatchedFile {
  source: string; // the picked path
  dest: string;
  size: number;
  info: GameFileInfo;
}

export interface SelectionMatch {
  root: string | null;
  matched: MatchedFile[];
  ignored: number;
}

/** Maps a picked selection onto game files. Duplicate destinations keep the first file. */
export function matchSelection(files: PickedFile[], opts: ClassifyOptions = {}): SelectionMatch {
  const root = detectRoot(files.map((f) => f.path));
  const rootLower = root?.toLowerCase() ?? null;
  const seen = new Set<string>();
  const matched: MatchedFile[] = [];
  let ignored = 0;
  for (const f of files) {
    const p = normalizePath(f.path);
    let info: GameFileInfo | null = null;
    if (rootLower !== null) {
      let rel: string | null = null;
      if (rootLower === "") rel = p;
      else if (p.toLowerCase().startsWith(`${rootLower}/`)) rel = p.slice(rootLower.length + 1);
      if (rel !== null) info = classifyGamePath(rel, opts);
    } else {
      // the user picked zone/Common or zone/<Language> itself: ".../Common/x.ff"
      const segs = p.split("/");
      const folder = segs[segs.length - 2];
      const file = segs[segs.length - 1];
      if (folder && file && (folder.toLowerCase() === "common" || findLanguage(folder))) {
        info = classifyGamePath(`zone/${folder}/${file}`, opts);
      }
    }
    if (!info) {
      ignored++;
      continue;
    }
    const key = info.dest.toLowerCase();
    if (seen.has(key)) {
      ignored++;
      continue;
    }
    seen.add(key);
    matched.push({ source: f.path, dest: info.dest, size: f.size, info });
  }
  return { root, matched, ignored };
}

/** The language the install runs in: localization.txt's first line. */
export function parseLocalizationLanguage(text: string): string | null {
  const first = text.split(/\r?\n/, 1)[0]?.trim().toLowerCase() ?? "";
  return first.length > 0 && first.length < 32 ? first : null;
}

export interface MapAvailability {
  available: boolean; // the map's own files are present (playable when core is complete too)
  missing: string[];
}

export interface Availability {
  language: Language | null; // the language the availability was computed for
  languagesFound: string[];
  core: { complete: boolean; missing: string[] };
  maps: Record<string, MapAvailability>;
  files: number;
  bytes: number;
}

/**
 * What a set of game files (paths relative to the game root) allows: is the core complete, which maps are present.
 * preferredLanguage is localization.txt's language when known (the engine loads that language's zones).
 */
export function computeAvailability(
  files: { path: string; size?: number }[],
  preferredLanguage?: string | null,
): Availability {
  const have = new Set<string>();
  const languagesFound = new Set<string>();
  let iwds = 0;
  const localizedIwds = new Map<string, number>();
  let bytes = 0;
  let count = 0;
  for (const f of files) {
    const info = classifyGamePath(f.path, { includeVideo: true });
    if (!info) continue;
    count++;
    bytes += f.size ?? 0;
    have.add(info.dest.toLowerCase());
    if (info.language) languagesFound.add(info.language);
    if (info.kind === "iwd") iwds++;
    if (info.kind === "localized-iwd" && info.language) {
      localizedIwds.set(info.language, (localizedIwds.get(info.language) ?? 0) + 1);
    }
  }
  const found = [...languagesFound];
  const language =
    findLanguage(preferredLanguage) ??
    (found.includes("english") ? findLanguage("english") : findLanguage(found[0])) ??
    null;
  const lang = language ?? LANGUAGES[0]!;
  const has = (p: string) => have.has(p.toLowerCase());
  const missing: string[] = [];
  for (const z of CORE_ZONES) if (!has(`zone/Common/${z}.ff`)) missing.push(`zone/Common/${z}.ff`);
  for (const z of CORE_LOCALIZED_REQUIRED) {
    const p = `zone/${lang.folder}/${lang.prefix}${z}.ff`;
    if (!has(p)) missing.push(p);
  }
  if (iwds === 0) missing.push("main/iw_*.iwd");
  if ((localizedIwds.get(lang.name) ?? 0) === 0) missing.push(`main/localized_${lang.name}_iwd*.iwd`);
  if (!has("localization.txt")) missing.push("localization.txt");
  const maps: Record<string, MapAvailability> = {};
  for (const m of MAPS) {
    const need = [`zone/Common/${m.id}.ff`, `zone/${lang.folder}/${lang.prefix}${m.id}.ff`];
    const miss = need.filter((p) => !has(p));
    maps[m.id] = { available: miss.length === 0, missing: miss };
  }
  return {
    language,
    languagesFound: found,
    core: { complete: missing.length === 0, missing },
    maps,
    files: count,
    bytes,
  };
}

/** True when these files let the player play `mapId` (core complete and the map's zones present). */
export function canPlayMap(av: Availability | null, mapId: string): boolean {
  return !!av && av.core.complete && !!av.maps[mapId]?.available;
}

export function playableMaps(av: Availability | null): string[] {
  return MAPS.filter((m) => canPlayMap(av, m.id)).map((m) => m.id);
}

export interface ImportPlan {
  copy: MatchedFile[];
  skip: MatchedFile[]; // already imported with the same size (resume)
  copyBytes: number;
  skipBytes: number;
}

/** Splits matched files into those to copy and those already present with the same size. */
export function planImport(matched: MatchedFile[], existing: Map<string, number>): ImportPlan {
  const plan: ImportPlan = { copy: [], skip: [], copyBytes: 0, skipBytes: 0 };
  for (const f of matched) {
    if (existing.get(f.dest.toLowerCase()) === f.size) {
      plan.skip.push(f);
      plan.skipBytes += f.size;
    } else {
      plan.copy.push(f);
      plan.copyBytes += f.size;
    }
  }
  return plan;
}

export function formatBytes(n: number): string {
  if (!Number.isFinite(n) || n < 0) return "?";
  if (n < 1024) return `${n} B`;
  const units = ["KB", "MB", "GB", "TB"];
  let v = n / 1024;
  let i = 0;
  while (v >= 1024 && i < units.length - 1) {
    v /= 1024;
    i++;
  }
  return `${v >= 100 ? v.toFixed(0) : v >= 10 ? v.toFixed(1) : v.toFixed(2)} ${units[i]}`;
}
