// import.ts - copies the player's own Black Ops files into OPFS (bo1/game/...). Nothing is uploaded.
//
// 1. The player picks a folder: window.showDirectoryPicker() (Chromium) or <input type=file webkitdirectory>
//    (Firefox, Safari). Several picks add up (e.g. zone/ and main/ separately).
// 2. Each pick is matched against manifest.ts (root detection, case-insensitive); the page shows what was found.
// 3. run() streams every needed file into OPFS (copy.ts), skipping files already present with the same size
//    (resume), with overall and per-file progress and cancel; then writes bo1/import.json.

import { MAPS } from "../../../shared/roster";
import { writeBlob, writeText, CopyCancelled } from "./copy";
import {
  classifyGamePath,
  computeAvailability,
  matchSelection,
  parseLocalizationLanguage,
  planImport,
  playableMaps,
  type Availability,
  type ImportPlan,
  type MatchedFile,
} from "./manifest";
import { DATA_INDEX, GAME_DIR, IMPORT_INDEX, listFiles, readJson, readText, removeEntry, requestPersist } from "./opfs";

export interface ImportIndex {
  files: Record<string, number>;
  maps: string[];
}

export interface ImportedState {
  index: ImportIndex;
  language: string | null;
  availability: Availability;
}

/** Reads bo1/import.json and localization.txt: what the player has imported. */
export async function loadImported(): Promise<ImportedState> {
  const raw = await readJson<Partial<ImportIndex>>(IMPORT_INDEX);
  const index: ImportIndex = {
    files: raw && typeof raw.files === "object" && raw.files ? raw.files : {},
    maps: Array.isArray(raw?.maps) ? raw.maps : [],
  };
  const locText = await readText(`${GAME_DIR}/localization.txt`);
  const language = locText ? parseLocalizationLanguage(locText) : null;
  const availability = computeAvailability(
    Object.entries(index.files).map(([path, size]) => ({ path, size })),
    language,
  );
  return { index, language, availability };
}

async function existingGameFiles(): Promise<Map<string, number>> {
  const m = new Map<string, number>();
  for (const f of await listFiles(GAME_DIR)) m.set(f.path.toLowerCase(), f.size);
  return m;
}

/** Rebuilds bo1/import.json from what is actually in OPFS (after an import, or from the files view). */
export async function rebuildIndex(): Promise<ImportedState> {
  const files: Record<string, number> = {};
  for (const f of await listFiles(GAME_DIR)) {
    const info = classifyGamePath(f.path, { includeVideo: true });
    if (info) files[f.path] = f.size;
  }
  const locText = await readText(`${GAME_DIR}/localization.txt`);
  const language = locText ? parseLocalizationLanguage(locText) : null;
  const availability = computeAvailability(
    Object.entries(files).map(([path, size]) => ({ path, size })),
    language,
  );
  const index: ImportIndex = { files, maps: playableMaps(availability) };
  await writeText(IMPORT_INDEX, JSON.stringify(index, null, 1));
  return { index, language, availability };
}

/** Removes the imported files, the synced data and both indexes (bo1/home, the engine's settings, stays). */
export async function deleteAllImported(): Promise<void> {
  await removeEntry(GAME_DIR);
  await removeEntry(IMPORT_INDEX);
  await removeEntry(DATA_INDEX);
}

// ----- picking -----

export interface SourceFile {
  path: string; // relative to the selection, starting with the picked folder's name
  size: number;
  file: () => Promise<File>;
}

const RELEVANT = /\.(ff|iwd|bik)$/i;
const SKIP_DIRS = new Set(["$recycle.bin", "system volume information", ".git", "node_modules", "redist"]);
const MAX_DEPTH = 8;

function relevant(name: string): boolean {
  return RELEVANT.test(name) || name.toLowerCase() === "localization.txt";
}

export function supportsDirectoryPicker(): boolean {
  return typeof (window as unknown as { showDirectoryPicker?: unknown }).showDirectoryPicker === "function";
}

interface IterableDir {
  name: string;
  entries(): AsyncIterable<[string, FileSystemHandle]>;
}

/** Opens the directory picker and lists the relevant files. Resolves null when the user cancels. */
export async function pickWithDirectoryPicker(onScan?: (filesSeen: number, dirsSeen: number) => void): Promise<SourceFile[] | null> {
  const picker = (window as unknown as { showDirectoryPicker: (o?: object) => Promise<FileSystemDirectoryHandle> })
    .showDirectoryPicker;
  let root: FileSystemDirectoryHandle;
  try {
    root = await picker.call(window, { id: "bo1-install", mode: "read" });
  } catch (e) {
    if (e instanceof DOMException && e.name === "AbortError") return null;
    throw e;
  }
  const out: SourceFile[] = [];
  let files = 0;
  let dirs = 0;
  const walk = async (dir: IterableDir, prefix: string, depth: number): Promise<void> => {
    dirs++;
    for await (const [name, handle] of dir.entries()) {
      if (handle.kind === "directory") {
        if (depth < MAX_DEPTH && !SKIP_DIRS.has(name.toLowerCase())) {
          await walk(handle as unknown as IterableDir, `${prefix}${name}/`, depth + 1);
        }
      } else {
        files++;
        if (relevant(name)) {
          const fh = handle as FileSystemFileHandle;
          const f = await fh.getFile();
          out.push({ path: `${prefix}${name}`, size: f.size, file: () => fh.getFile() });
        }
      }
      if ((files + dirs) % 200 === 0) onScan?.(files, dirs);
    }
  };
  await walk(root as unknown as IterableDir, `${root.name}/`, 0);
  onScan?.(files, dirs);
  return out;
}

/** The <input type=file webkitdirectory> fallback. */
export function sourcesFromFileList(list: FileList | File[]): SourceFile[] {
  const out: SourceFile[] = [];
  for (const f of Array.from(list)) {
    if (!relevant(f.name)) continue;
    out.push({ path: f.webkitRelativePath || f.name, size: f.size, file: async () => f });
  }
  return out;
}

// ----- the import session -----

export interface ImportProgress {
  filesTotal: number;
  filesDone: number;
  bytesTotal: number;
  bytesDone: number;
  current: string | null;
  currentBytes: number;
  currentSize: number;
  bytesPerSec: number;
}

export interface ImportOutcome {
  copied: number;
  skipped: number;
  cancelled: boolean;
  error: string | null;
  state: ImportedState;
}

interface Selected extends MatchedFile {
  src: SourceFile;
}

export class ImportSession {
  private batches: SourceFile[][] = [];
  includeVideo = false;
  existing = new Map<string, number>();
  ignored = 0;
  roots: (string | null)[] = [];

  async init(): Promise<void> {
    this.existing = await existingGameFiles();
  }

  get empty(): boolean {
    return this.batches.length === 0;
  }

  add(files: SourceFile[]): void {
    this.batches.push(files);
  }

  clear(): void {
    this.batches = [];
  }

  /** Matched files of every pick (each pick has its own root), de-duplicated by destination. */
  selected(): Selected[] {
    const seen = new Set<string>();
    const out: Selected[] = [];
    this.ignored = 0;
    this.roots = [];
    for (const batch of this.batches) {
      const bySource = new Map(batch.map((s) => [s.path, s]));
      const m = matchSelection(batch, { includeVideo: this.includeVideo });
      this.roots.push(m.root);
      this.ignored += m.ignored;
      for (const f of m.matched) {
        const key = f.dest.toLowerCase();
        const src = bySource.get(f.source);
        if (seen.has(key) || !src) continue;
        seen.add(key);
        out.push({ ...f, src });
      }
    }
    return out;
  }

  plan(): ImportPlan & { selected: Selected[] } {
    const selected = this.selected();
    return { ...planImport(selected, this.existing), selected };
  }

  /** Availability once the selection is imported (what is already imported plus what was picked). */
  projected(language: string | null, selected = this.selected()): Availability {
    const all = new Map<string, { path: string; size: number }>();
    for (const [path, size] of this.existing) all.set(path, { path, size });
    for (const f of selected) all.set(f.dest.toLowerCase(), { path: f.dest, size: f.size });
    return computeAvailability([...all.values()], language);
  }

  /** The language of the picked localization.txt (if one was picked), else null. */
  async pickedLanguage(selected = this.selected()): Promise<string | null> {
    const loc = selected.find((f) => f.info.kind === "localization");
    if (!loc) return null;
    try {
      return parseLocalizationLanguage(await (await loc.src.file()).slice(0, 256).text());
    } catch {
      return null;
    }
  }

  async run(onProgress: (p: ImportProgress) => void, signal: AbortSignal): Promise<ImportOutcome> {
    await requestPersist();
    const plan = this.plan();
    const progress: ImportProgress = {
      filesTotal: plan.copy.length,
      filesDone: 0,
      bytesTotal: plan.copyBytes,
      bytesDone: 0,
      current: null,
      currentBytes: 0,
      currentSize: 0,
      bytesPerSec: 0,
    };
    const started = performance.now();
    let error: string | null = null;
    let cancelled = false;
    let copied = 0;
    const emit = () => {
      const secs = (performance.now() - started) / 1000;
      progress.bytesPerSec = secs > 0.5 ? (progress.bytesDone + progress.currentBytes) / secs : 0;
      onProgress({ ...progress });
    };
    // biggest files last so the small zones (and a usable core) land first
    const order = [...(plan.copy as Selected[])].sort((a, b) => a.size - b.size);
    for (const f of order) {
      if (signal.aborted) {
        cancelled = true;
        break;
      }
      progress.current = f.dest;
      progress.currentBytes = 0;
      progress.currentSize = f.size;
      emit();
      try {
        const file = await f.src.file();
        const written = await writeBlob(
          `${GAME_DIR}/${f.dest}`,
          file,
          (n) => {
            progress.currentBytes = n;
            emit();
          },
          signal,
        );
        if (written !== f.size) throw new Error(`${f.dest}: wrote ${written} of ${f.size} bytes`);
        this.existing.set(f.dest.toLowerCase(), f.size);
        copied++;
        progress.filesDone++;
        progress.bytesDone += f.size;
        progress.currentBytes = 0;
        emit();
      } catch (e) {
        if (e instanceof CopyCancelled || signal.aborted) {
          cancelled = true;
        } else {
          error = e instanceof Error ? e.message : String(e);
          if (/quota|space|full/i.test(error)) error = `Not enough storage space for ${f.dest}. ${error}`;
        }
        break;
      }
    }
    progress.current = null;
    emit();
    const state = await rebuildIndex();
    return { copied, skipped: plan.skip.length, cancelled, error, state };
  }
}

export function mapNames(ids: string[]): string {
  return ids.map((id) => MAPS.find((m) => m.id === id)?.name ?? id).join(", ");
}
