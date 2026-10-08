// filesview.ts - the "Game files" screen: import the player's own files into the browser, see what is there,
// delete everything. Nothing is uploaded: files are copied into this site's private browser storage (OPFS).

import { MAPS } from "../../../shared/roster";
import type { App } from "../app";
import { nav } from "../input/nav";
import { clear, confirmDialog, h, put, toast } from "../ui/dom";
import { icons } from "../ui/icons";
import { Disposer, type ScreenInstance } from "../ui/screen";
import {
  deleteAllImported,
  ImportSession,
  pickWithDirectoryPicker,
  rebuildIndex,
  sourcesFromFileList,
  supportsDirectoryPicker,
  type ImportProgress,
} from "./import";
import { formatBytes, type Availability } from "./manifest";
import { opfsAvailable, storageInfo, type StorageInfo } from "./opfs";

function duration(sec: number): string {
  if (!Number.isFinite(sec) || sec <= 0) return "";
  if (sec < 60) return `${Math.ceil(sec)} s`;
  if (sec < 3600) return `${Math.ceil(sec / 60)} min`;
  return `${(sec / 3600).toFixed(1)} h`;
}

function bar(fraction: number, testid?: string): HTMLElement {
  const pct = Math.max(0, Math.min(1, fraction)) * 100;
  return h(
    "div",
    { class: "bar", role: "progressbar", "aria-valuemin": "0", "aria-valuemax": "100", "aria-valuenow": pct.toFixed(0), "data-testid": testid },
    h("div", { class: "bar-fill", style: `width:${pct.toFixed(1)}%` }),
  );
}

export function availabilityView(av: Availability | null, opts: { title?: string } = {}): HTMLElement {
  const box = h("div", { class: "availability" });
  if (opts.title) put(box, h("h3", null, opts.title));
  if (!av || av.files === 0) {
    put(box, h("p", { class: "muted" }, "Nothing imported yet."));
    return box;
  }
  put(box,
    h(
      "p",
      { class: av.core.complete ? "ok" : "warn", "data-testid": "core-status" },
      av.core.complete ? icons.check() : icons.cross(),
      av.core.complete ? "Core files complete" : `Core files incomplete: ${av.core.missing.length} missing`,
      av.language ? h("span", { class: "muted" }, ` · language: ${av.language.name}`) : null,
    ),
  );
  if (!av.core.complete) {
    put(box, h("ul", { class: "missing-list" }, av.core.missing.slice(0, 12).map((m) => h("li", null, h("code", null, m)))));
    if (av.core.missing.length > 12) put(box, h("p", { class: "muted small" }, `…and ${av.core.missing.length - 12} more`));
  }
  put(box,
    h(
      "ul",
      { class: "map-list", "data-testid": "map-availability" },
      MAPS.map((m) => {
        const a = av.maps[m.id];
        const playable = av.core.complete && !!a?.available;
        return h(
          "li",
          { class: playable ? "ok" : a?.available ? "warn" : "muted", "data-map": m.id },
          playable ? icons.check() : icons.cross(),
          h("span", null, m.name),
          !a?.available ? h("span", { class: "small muted" }, " · map files missing") : !playable ? h("span", { class: "small" }, " · needs the core files") : null,
        );
      }),
    ),
  );
  return box;
}

// The import outlives the screen: leaving "Game files" while copying keeps the copy going, and coming back shows it.
const session = new ImportSession();
let running: AbortController | null = null;
let progress: ImportProgress | null = null;
let lastResult: string | null = null;
let mounted: { progress(): void; all(): void } | null = null;

export function importRunning(): boolean {
  return running !== null;
}

export function mountFiles(host: HTMLElement, app: App): ScreenInstance {
  const d = new Disposer();
  let storage: StorageInfo = { usage: null, quota: null, persisted: null };
  let scanning: string | null = null;

  const manageBox = h("div", { class: "card", "data-testid": "imported-files" });
  const importBox = h("div", { class: "card", "data-testid": "importer" });
  const storageBox = h("div", { class: "card" });

  const fileInput = h("input", { type: "file", multiple: true, hidden: true, "data-testid": "folder-input" });
  fileInput.setAttribute("webkitdirectory", "");
  fileInput.setAttribute("directory", "");
  fileInput.addEventListener("change", () => {
    if (!fileInput.files || fileInput.files.length === 0) return;
    session.add(sourcesFromFileList(fileInput.files));
    fileInput.value = "";
    lastResult = null;
    void renderImport();
  });

  const pick = async () => {
    if (!supportsDirectoryPicker()) {
      fileInput.click();
      return;
    }
    try {
      scanning = "Scanning the folder…";
      void renderImport();
      const files = await pickWithDirectoryPicker((f, dirs) => {
        scanning = `Scanning… ${f} files in ${dirs} folders`;
        void renderImport();
      });
      scanning = null;
      if (files) {
        session.add(files);
        lastResult = null;
      }
    } catch (e) {
      scanning = null;
      toast(`Could not read the folder: ${e instanceof Error ? e.message : String(e)}`, "error");
    }
    void renderImport();
  };

  const startImport = async () => {
    if (running) return;
    running = new AbortController();
    progress = null;
    void renderImport();
    const outcome = await session.run((p) => {
      progress = p;
      mounted?.progress();
    }, running.signal);
    running = null;
    progress = null;
    app.setFiles(outcome.state);
    if (outcome.error) {
      lastResult = `Import stopped: ${outcome.error}`;
      toast(lastResult, "error", 8000);
    } else if (outcome.cancelled) {
      lastResult = `Import cancelled after ${outcome.copied} file(s). Pick the same folder again to resume.`;
      toast("Import cancelled.", "warn");
    } else {
      lastResult = `Imported ${outcome.copied} file(s)${outcome.skipped ? `, ${outcome.skipped} already there` : ""}.`;
      toast(lastResult, "success");
      session.clear();
    }
    mounted?.all();
  };

  // ----- imported files -----
  const renderManage = () => {
    const focusKey = nav.focusKey();
    clear(manageBox);
    const av = app.files?.availability ?? null;
    put(manageBox,
      h("h2", null, "In this browser"),
      availabilityView(av),
      av && av.files > 0 ? h("p", { class: "muted small" }, `${av.files} files, ${formatBytes(av.bytes)}`) : null,
      h(
        "div",
        { class: "row" },
        h(
          "button",
          {
            class: "btn small",
            "data-nav-id": "rescan",
            "data-testid": "rescan-files",
            disabled: !!running || !opfsAvailable(),
            onclick: async () => {
              app.setFiles(await rebuildIndex());
              await session.init();
              toast("Storage rescanned.", "info", 2000);
              renderManage();
            },
          },
          "Rescan",
        ),
        h(
          "button",
          {
            class: "btn small danger",
            "data-nav-id": "delete-all",
            "data-testid": "delete-files",
            disabled: !!running || !av || av.files === 0,
            onclick: async () => {
              if (!(await confirmDialog("Delete all game files?", "This removes every imported file and the mods/data copies from this browser. You can import them again later.", "Delete", "Keep"))) return;
              await deleteAllImported();
              await session.init();
              await app.refreshFiles();
              storage = await storageInfo();
              renderStorage();
              renderManage();
              toast("Game files deleted.", "info");
            },
          },
          "Delete all",
        ),
      ),
    );
    nav.restoreFocus(focusKey);
  };

  // ----- importer -----
  const progressBox = h("div", { class: "progress-box", "data-testid": "import-progress" });
  const renderProgress = () => {
    clear(progressBox);
    const p = progress;
    if (!p) {
      put(progressBox, h("p", { class: "muted" }, "Starting…"));
      return;
    }
    const done = p.bytesDone + p.currentBytes;
    const remaining = p.bytesTotal - done;
    const eta = p.bytesPerSec > 0 ? duration(remaining / p.bytesPerSec) : "";
    put(progressBox,
      h("p", null, `${formatBytes(done)} of ${formatBytes(p.bytesTotal)} · file ${Math.min(p.filesDone + 1, p.filesTotal)} of ${p.filesTotal}`, p.bytesPerSec ? ` · ${formatBytes(p.bytesPerSec)}/s` : "", eta ? ` · about ${eta} left` : ""),
      bar(p.bytesTotal ? done / p.bytesTotal : 1, "import-overall"),
      p.current
        ? h("p", { class: "small muted" }, h("code", null, p.current), ` ${formatBytes(p.currentBytes)} / ${formatBytes(p.currentSize)}`)
        : null,
      p.current ? bar(p.currentSize ? p.currentBytes / p.currentSize : 1, "import-file") : null,
    );
  };

  let renderSeq = 0;
  const renderImport = async () => {
    // the async part first, so overlapping renders cannot interleave their DOM writes
    const seq = ++renderSeq;
    const pickedLanguage = !session.empty && !running ? await session.pickedLanguage() : null;
    if (seq !== renderSeq) return;
    const focusKey = nav.focusKey();
    clear(importBox);
    put(importBox, h("h2", null, "Import from your Black Ops install"), fileInput);
    if (!opfsAvailable()) {
      put(importBox, h("p", { class: "warn" }, "This browser has no private file storage (OPFS), so game files cannot be imported. Private windows often disable it."));
      return;
    }
    if (running) {
      renderProgress();
      put(importBox,
        progressBox,
        h(
          "button",
          {
            class: "btn danger",
            "data-testid": "cancel-import",
            "data-nav-id": "cancel-import",
            "data-autofocus": true,
            onclick: () => running?.abort(),
          },
          "Cancel",
        ),
        h("p", { class: "muted small" }, "You can cancel at any time; picking the same folder again resumes where it stopped."),
      );
      nav.restoreFocus(focusKey);
      if (nav.mode !== "mouse" && !nav.current()) nav.focusFirst(importBox);
      return;
    }
    const pickLabel = session.empty ? "Choose your Black Ops folder" : "Add another folder";
    put(importBox,
      h(
        "ol",
        { class: "steps" },
        h("li", null, "Pick the folder Black Ops is installed in (for Steam: ", h("code", null, "steamapps/common/Call of Duty Black Ops"), "). Its parent, or the ", h("code", null, "zone"), " and ", h("code", null, "main"), " folders one after the other, work too."),
        h("li", null, "Check what was found, then start the import. Large files take a while; you can cancel and resume."),
      ),
      h(
        "div",
        { class: "row" },
        h("button", { class: ["btn", session.empty && "primary"], "data-testid": "pick-folder", "data-nav-id": "pick-folder", onclick: () => void pick() }, icons.folder(), pickLabel),
        supportsDirectoryPicker()
          ? h("button", { class: "btn small", "data-nav-id": "pick-fallback", onclick: () => fileInput.click() }, "Use the browser's upload dialog")
          : null,
      ),
      supportsDirectoryPicker()
        ? null
        : h("p", { class: "muted small" }, "Your browser will ask to \"upload\" the folder: that is its name for reading files. Nothing is sent anywhere; the files are copied into this browser only."),
    );
    if (scanning) put(importBox, h("p", { class: "muted" }, scanning));
    if (lastResult) put(importBox, h("p", { class: "small" }, lastResult));
    if (!session.empty) {
      const plan = session.plan();
      const language = pickedLanguage ?? app.files?.language ?? null;
      const projected = session.projected(language, plan.selected);
      const free = storage.quota !== null && storage.usage !== null ? storage.quota - storage.usage : null;
      const videoCb = h("input", { type: "checkbox", id: "include-video", checked: session.includeVideo });
      videoCb.addEventListener("change", () => {
        session.includeVideo = videoCb.checked;
        void renderImport();
      });
      const roots = session.roots.map((r) => (r === null ? "(no zone/ or main/ folder found)" : r === "" ? "(the folder you picked)" : r));
      put(importBox,
        h(
          "div",
          { class: "scan-result", "data-testid": "scan-result" },
          h("p", null, `Found ${plan.selected.length} game file${plan.selected.length === 1 ? "" : "s"}: `, h("strong", null, `${plan.copy.length} to copy (${formatBytes(plan.copyBytes)})`), plan.skip.length ? `, ${plan.skip.length} already imported (${formatBytes(plan.skipBytes)})` : "", "."),
          h("p", { class: "muted small" }, `Game folder: ${roots.join("; ")} · ${session.ignored} other files ignored (multiplayer, campaign, executables).`),
          plan.selected.length === 0
            ? h("p", { class: "warn" }, "No Zombies files in that folder. Pick the folder that contains zone/ and main/.")
            : null,
          free !== null && plan.copyBytes > free
            ? h("p", { class: "warn" }, `This needs ${formatBytes(plan.copyBytes)} but the browser offers about ${formatBytes(free)}. Free disk space or import fewer maps.`)
            : null,
          availabilityView(projected, { title: "After the import" }),
          h("label", { class: "toggle", for: "include-video" }, videoCb, h("span", null, "Also import the cinematics (main/video, large, not used yet)")),
          h(
            "div",
            { class: "row" },
            h(
              "button",
              {
                class: "btn primary",
                "data-testid": "start-import",
                "data-nav-id": "start-import",
                disabled: plan.copy.length === 0,
                onclick: () => void startImport(),
              },
              plan.copy.length ? `Import ${formatBytes(plan.copyBytes)}` : "Nothing new to import",
            ),
            h(
              "button",
              {
                class: "btn small",
                "data-nav-id": "clear-selection",
                onclick: () => {
                  session.clear();
                  void renderImport();
                },
              },
              "Clear selection",
            ),
          ),
        ),
      );
    }
    nav.restoreFocus(focusKey);
  };

  const renderStorage = () => {
    clear(storageBox);
    const used = storage.usage;
    const quota = storage.quota;
    put(storageBox,
      h("h2", null, "Browser storage"),
      used !== null && quota !== null
        ? [h("p", null, `${formatBytes(used)} used of about ${formatBytes(quota)} available to this site.`), bar(quota ? used / quota : 0)]
        : h("p", { class: "muted" }, "This browser does not report its storage quota."),
      h(
        "p",
        { class: "small muted" },
        storage.persisted
          ? "Storage is persistent: the browser will not clear it on its own."
          : "Storage is not marked persistent yet; the import asks the browser to keep it. Clearing site data removes the files.",
      ),
    );
  };

  put(host,
    h(
      "section",
      { class: "screen files", "data-testid": "files-screen" },
      h("h1", null, "Game files"),
      h(
        "div",
        { class: "banner banner-privacy" },
        icons.shield(),
        h(
          "div",
          null,
          h("strong", null, "Your files stay on this computer. "),
          "They are copied into this site's private browser storage (the Origin Private File System) and read from there by the game. Nothing you import is uploaded, and the server never sees it.",
        ),
      ),
      h("div", { class: "grid two" }, importBox, h("div", { class: "col" }, manageBox, storageBox)),
    ),
  );

  const refreshAll = async () => {
    if (opfsAvailable() && !running) await session.init();
    storage = await storageInfo();
    renderStorage();
    renderManage();
    void renderImport();
  };
  mounted = { progress: renderProgress, all: () => void refreshAll() };
  renderManage();
  renderStorage();
  void renderImport();
  void refreshAll();
  d.add(app.on((what) => {
    if (what === "files") renderManage();
  }));

  nav.setScreen(
    [{ button: "y", label: "Pick folder", key: "p", run: () => void pick() }],
    () => {
      if (running) return;
      app.go(app.lobby.room ? "lobby" : "home");
    },
  );
  if (nav.mode !== "mouse") nav.focusFirst(host);

  return {
    unmount() {
      d.dispose();
      mounted = null; // an import keeps running in the background
    },
  };
}
